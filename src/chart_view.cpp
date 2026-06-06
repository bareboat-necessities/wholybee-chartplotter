#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "projection.hpp"
#include "geom_clip.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QTimer>
#include <QPen>
#include <QBrush>
#include <QThread>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

QColor fillColor(const Feature& f) {
    if (f.kind == FeatureKind::LandArea) return QColor(217, 199, 148);
    if (!f.hasDepth)                     return QColor(184, 212, 235);
    double d = f.depth;
    if (d <  0.0)  return QColor(158, 189, 140);
    if (d <  2.0)  return QColor(102, 168, 217);
    if (d <  5.0)  return QColor(143, 194, 227);
    if (d < 10.0)  return QColor(184, 217, 240);
    if (d < 20.0)  return QColor(217, 235, 250);
    return QColor(242, 247, 255);
}

// Builds a path in scene coordinates: projected X, and Y negated so north is up.
QPainterPath buildPathFromRings(const std::vector<std::vector<Pt>>& rings, bool closed) {
    QPainterPath path;
    if (closed) path.setFillRule(Qt::OddEvenFill);
    for (const auto& ring : rings) {
        if (ring.size() < 2) continue;
        path.moveTo(ring[0].x, -ring[0].y);
        for (std::size_t i = 1; i < ring.size(); ++i)
            path.lineTo(ring[i].x, -ring[i].y);
        if (closed) path.closeSubpath();
    }
    return path;
}

using geom::clipRingToRect;
using geom::clipPolylineToRect;
using geom::pointInRect;

// Rough in-memory footprint of a parsed cell, for the LRU byte budget.
std::size_t approxBytes(const std::vector<Feature>& feats) {
    std::size_t b = sizeof(Feature) * feats.capacity();
    for (const Feature& f : feats) {
        b += sizeof(std::vector<Pt>) * f.rings.capacity();
        for (const auto& ring : f.rings)
            b += sizeof(Pt) * ring.capacity();
    }
    return b;
}

// Vertex-merge tolerance (projected metres) for a usage band, ~half a pixel at
// the band's display scale. Band-based so geometry never needs re-simplifying as
// you zoom within a band; the finest bands keep full detail.
double simplifyToleranceM(int band) {
    switch (band) {
        case 1: return 1389.0;   // overview
        case 2: return 278.0;    // general
        case 3: return 46.0;     // coastal
        case 4: return 13.9;     // approach
        case 5: return 2.8;      // harbour
        default: return 0.0;     // berthing / unknown: keep full detail
    }
}

// Resolve where the GSHHG basemap lives. Search order: an explicit override
// (the basemap-folder setting), next to the executable, the per-user and shared
// data locations, and finally the in-tree dev folder. The first root that holds
// a recognizable layout wins. All tiers present are returned; which one is used
// is chosen by zoom at draw time (see ChartView::desiredTier).
struct BasemapSource { QString root; QStringList tiers; };

QStringList tiersIn(const QString& root) {
    QStringList out;
    static const char* all[] = {"c", "l", "i", "h", "f"};
    for (const char* t : all) {
        const QString f = root + "/GSHHS_shp/" + t + "/GSHHS_" + t + "_L1.shp";
        if (QFileInfo::exists(f)) out << QString::fromLatin1(t);
    }
    return out;
}

BasemapSource resolveBasemap(const QString& override) {
    QStringList roots;
    if (!override.isEmpty()) roots << override;
    roots << QCoreApplication::applicationDirPath() + "/gshhg-shp";
    for (const QString& d : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation))
        roots << d + "/gshhg-shp";
    for (const QString& d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        roots << d + "/gshhg-shp";
#ifdef CHARTPLOTTER_SOURCE_DIR
    roots << QString::fromLatin1(CHARTPLOTTER_SOURCE_DIR) + "/gshhg-shp";
#endif
    for (const QString& r : roots) {
        const QStringList ts = tiersIn(r);
        if (!ts.isEmpty()) return { r, ts };
    }
    return {};
}

// Clip + simplify one cell to `clipBox` (projected, real frame) with vertex-merge
// tolerance `tol`, building its vector primitives sorted by z. Pure and
// Qt-value-only: runs on a worker. Used for both ENC cells and the basemap.
BuiltCell buildCell(const QString& path, const std::vector<Feature>& feats,
                    int band, const BBox& clipBox, double tol) {
    BuiltCell bc;
    bc.path    = path;
    bc.band    = band;
    bc.clipBox = clipBox;

    const double zb = static_cast<double>(band) * 1000.0;

    std::vector<std::vector<Pt>> clipBuf;
    std::vector<std::vector<Pt>> simpBuf;

    auto reduce = [&](const std::vector<std::vector<Pt>>& src, std::size_t minPts)
            -> const std::vector<std::vector<Pt>>& {
        if (tol <= 0.0) return src;
        simpBuf.clear();
        for (const auto& ring : src) {
            std::vector<Pt> s = geom::simplify(ring, tol);
            if (s.size() >= minPts) simpBuf.push_back(std::move(s));
        }
        return simpBuf;
    };

    for (const Feature& f : feats) {
        const bool doClip = clipBox.valid() && !clipBox.contains(f.bbox);

        switch (f.kind) {
            case FeatureKind::DepthArea:
            case FeatureKind::LandArea: {
                const std::vector<std::vector<Pt>>* rings = &f.rings;
                if (doClip) {
                    clipBuf.clear();
                    for (const auto& ring : f.rings) {
                        std::vector<Pt> c = clipRingToRect(ring, clipBox);
                        if (c.size() >= 3) clipBuf.push_back(std::move(c));
                    }
                    if (clipBuf.empty()) break;
                    rings = &clipBuf;
                }
                const std::vector<std::vector<Pt>>& use = reduce(*rings, 3);
                if (use.empty()) break;

                BuiltPath bp;
                bp.path   = buildPathFromRings(use, true);
                bp.bounds = bp.path.boundingRect();
                bp.z      = zb + f.zorder;
                bp.filled = true;
                bp.brush  = fillColor(f);
                if (f.kind == FeatureKind::LandArea) {
                    bp.hasPen = true; bp.penColor = QColor(115, 97, 64); bp.penWidth = 1.0;
                }
                bc.paths.push_back(std::move(bp));
                break;
            }
            case FeatureKind::OtherArea:
            case FeatureKind::DepthContour:
            case FeatureKind::Coastline:
            case FeatureKind::OtherLine: {
                const std::vector<std::vector<Pt>>* rings = &f.rings;
                if (doClip) {
                    clipBuf.clear();
                    for (const auto& ring : f.rings) {
                        std::vector<std::vector<Pt>> runs = clipPolylineToRect(ring, clipBox);
                        for (auto& run : runs) clipBuf.push_back(std::move(run));
                    }
                    if (clipBuf.empty()) break;
                    rings = &clipBuf;
                }
                const std::vector<std::vector<Pt>>& use = reduce(*rings, 2);
                if (use.empty()) break;

                BuiltPath bp;
                bp.path   = buildPathFromRings(use, false);
                bp.bounds = bp.path.boundingRect();
                bp.z      = zb + f.zorder;
                bp.filled = false;
                bp.hasPen = true;
                if (f.kind == FeatureKind::Coastline)        { bp.penColor = QColor(64, 51, 31);        bp.penWidth = 1.4; }
                else if (f.kind == FeatureKind::DepthContour){ bp.penColor = QColor(115, 153, 199);     bp.penWidth = 0.8; }
                else if (f.kind == FeatureKind::OtherArea)   { bp.penColor = QColor(102, 102, 115, 150); bp.penWidth = 0.7; }
                else                                         { bp.penColor = QColor(102, 102, 128);     bp.penWidth = 0.8; }
                bp.isDepthContour = (f.kind == FeatureKind::DepthContour);
                bc.paths.push_back(std::move(bp));
                break;
            }
            case FeatureKind::Sounding: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                if (doClip && !pointInRect(f.rings[0][0], clipBox)) break;
                const QString text = f.hasDepth
                    ? QString::number(f.depth, 'f', f.depth < 10.0 ? 1 : 0)
                    : QStringLiteral(".");
                bc.soundings.emplace_back(QPointF(f.rings[0][0].x, -f.rings[0][0].y), text);
                break;
            }
            case FeatureKind::Point: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                if (doClip && !pointInRect(f.rings[0][0], clipBox)) break;
                bc.symbols.emplace_back(f.rings[0][0].x, -f.rings[0][0].y);
                break;
            }
        }
    }

    std::sort(bc.paths.begin(), bc.paths.end(),
              [](const BuiltPath& a, const BuiltPath& b) { return a.z < b.z; });
    return bc;
}

} // namespace

ChartView::ChartView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, true);   // we fill the whole widget

    pool_.setMaxThreadCount(qBound(2, QThread::idealThreadCount(), 8));
    cache_.setLimits(256u * 1024u * 1024u, 256);
    cache_.setPinned([this](const QString& path) { return loaded_.contains(path); });

    // Basemap tiers: a small byte-budgeted LRU. The active tier is pinned so it
    // is never evicted; the cheap tiers (c/l/i) stay resident within budget while
    // the large ones (h/f) are dropped once you zoom away from them.
    tierCache_.setLimits(192u * 1024u * 1024u, 8);
    tierCache_.setPinned([this](const QString& tier) { return tier == basemapTier_; });

    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(120);
    connect(updateTimer_, &QTimer::timeout, this, [this] {
        updateVisibleCells();
        maybeBuildBasemap();
    });

    aaTimer_ = new QTimer(this);
    aaTimer_->setSingleShot(true);
    aaTimer_->setInterval(180);
    connect(aaTimer_, &QTimer::timeout, this, [this] { interacting_ = false; update(); });

    saveTimer_ = new QTimer(this);
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(500);
    connect(saveTimer_, &QTimer::timeout, this, [this] {
        double lon, lat, scale;
        if (currentView(lon, lat, scale)) emit viewChanged(lon, lat, scale);
    });
}

void ChartView::setCatalog(ChartCatalog* catalog) {
    catalog_ = catalog;
    if (catalog_)
        connect(catalog_, &ChartCatalog::finished, this, &ChartView::onCatalogFinished);
}

// ---- camera ----------------------------------------------------------------

double ChartView::worldWidthM() { return 2.0 * proj::lonToX(180.0); }

QTransform ChartView::cameraTransform() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(ppm_, ppm_);
    t.translate(-scx_, -scy_);
    return t;
}

QPointF ChartView::screenToScene(const QPointF& s) const {
    if (ppm_ <= 0.0) return QPointF();
    return QPointF((s.x() - width() / 2.0) / ppm_ + scx_,
                   (s.y() - height() / 2.0) / ppm_ + scy_);
}

void ChartView::normalizeCenter() {
    const double W = proj::lonToX(180.0);
    const double ww = worldWidthM();
    while (scx_ >=  W) scx_ -= ww;
    while (scx_ <  -W) scx_ += ww;
}

// The whole-world shift that brings a cell (centered at cellCenterX in scene X)
// nearest to the current view center — i.e. which side of the 180° seam to draw
// it on. Normally 0; ±worldWidth only for cells across the seam from the view.
double ChartView::wrapOffsetFor(double cellCenterX) const {
    const double ww = worldWidthM();
    return std::round((scx_ - cellCenterX) / ww) * ww;
}

void ChartView::restoreView(double lon, double lat, double s) {
    if (s <= 0.0) { fitToCatalog(); return; }
    scx_ = proj::lonToX(lon);
    scy_ = -proj::latToY(lat);
    ppm_ = s;
    normalizeCenter();
    updatePointLOD();
    update();
}

bool ChartView::currentView(double& lon, double& lat, double& s) const {
    if (!haveCatalog_ || ppm_ <= 0.0) return false;
    lon = proj::xToLon(scx_);
    lat = proj::yToLat(-scy_);
    s = ppm_;
    return true;
}

// ---- catalog / fit ---------------------------------------------------------

void ChartView::clearAll() {
    loaded_.clear();
    inFlight_.clear();
    building_.clear();
    wanted_.clear();
    bandByPath_.clear();
    bboxByPath_.clear();
    cache_.clear();
    pointLodVisible_ = true;
}

void ChartView::onCatalogFinished(bool ok, const QString&) {
    ++generation_;
    clearAll();
    haveCatalog_ = ok && catalog_ && catalog_->bounds().valid();
    userInteracted_ = false;

    if (!haveCatalog_) {
        emit statusChanged(QString());
        update();
        return;
    }

    bandByPath_.reserve(static_cast<int>(catalog_->cells().size()));
    bboxByPath_.reserve(static_cast<int>(catalog_->cells().size()));
    for (const CellRecord& c : catalog_->cells()) {
        bandByPath_.insert(c.path, c.band);
        if (c.extentValid) bboxByPath_.insert(c.path, c.bbox);
    }

    if (havePendingView_) {
        restoreView(pendingLon_, pendingLat_, pendingScale_);
        havePendingView_ = false;
        userInteracted_ = true;
    } else {
        fitToCatalog();
    }
    updateVisibleCells();
    maybeBuildBasemap();
    update();
}

void ChartView::fitToCatalog() {
    if (!haveCatalog_ || !catalog_ || width() <= 0 || height() <= 0) return;
    const BBox& b = catalog_->bounds();
    if (!b.valid()) return;
    const double wM = b.maxx - b.minx, hM = b.maxy - b.miny;
    if (wM <= 0.0 || hM <= 0.0) return;

    const double ppmW = (width()  * 0.92) / wM;
    const double ppmH = (height() * 0.92) / hM;
    ppm_ = std::max(1e-9, std::min(ppmW, ppmH));
    scx_ = (b.minx + b.maxx) / 2.0;
    scy_ = -(b.miny + b.maxy) / 2.0;
    normalizeCenter();
    updatePointLOD();
    update();
}

// ---- viewport-driven cell selection ---------------------------------------

int ChartView::bandForVisibleWidth(double metres) {
    const double nm = metres / 1852.0;
    if (nm > 1500.0) return 1;
    if (nm >  300.0) return 2;
    if (nm >   50.0) return 3;
    if (nm >   15.0) return 4;
    if (nm >    3.0) return 5;
    return 6;
}

BBox ChartView::expandBox(const BBox& b, double frac) {
    double dx = (b.maxx - b.minx) * frac;
    double dy = (b.maxy - b.miny) * frac;
    BBox r;
    r.minx = b.minx - dx; r.maxx = b.maxx + dx;
    r.miny = b.miny - dy; r.maxy = b.maxy + dy;
    return r;
}

BBox ChartView::shiftX(const BBox& b, double dx) {
    BBox r = b;
    r.minx += dx; r.maxx += dx;
    return r;
}

void ChartView::scheduleUpdate() {
    if (ppm_ > 0.0) updateTimer_->start();   // cells need a catalog; basemap doesn't
}

bool ChartView::computeViewBoxes(BBox& view, BBox& wanted, BBox& keep, int& target) const {
    if (ppm_ <= 0.0 || width() <= 0) return false;
    const double halfW = (width()  / 2.0) / ppm_;
    const double halfH = (height() / 2.0) / ppm_;
    // Scene -> projected (north up): proj x = sx, proj y = -sy.
    view.minx = scx_ - halfW; view.maxx = scx_ + halfW;
    view.miny = -(scy_ + halfH); view.maxy = -(scy_ - halfH);

    const double visWidthM = width() / ppm_;
    target = bandForVisibleWidth(visWidthM);
    wanted = expandBox(view, 0.5);
    keep   = expandBox(view, 1.5);
    return true;
}

void ChartView::updateVisibleCells() {
    if (!haveCatalog_ || !catalog_) return;

    BBox view, wantedArea, keepArea;
    int target;
    if (!computeViewBoxes(view, wantedArea, keepArea, target)) return;

    const std::vector<CellRecord>& cells = catalog_->cells();

    // Which bands have coverage in view? (wrap-aware via per-cell offset.)
    bool present[7] = {false,false,false,false,false,false,false};
    for (const CellRecord& c : cells) {
        if (!c.extentValid || c.band < 1 || c.band > 6) continue;
        const double off = wrapOffsetFor((c.bbox.minx + c.bbox.maxx) / 2.0);
        if (shiftX(c.bbox, off).intersects(wantedArea)) present[c.band] = true;
    }

    int maxBand = target;
    bool haveAtOrBelow = false;
    for (int b = 1; b <= target; ++b) if (present[b]) { haveAtOrBelow = true; break; }
    if (!haveAtOrBelow)
        for (int b = target + 1; b <= 6; ++b) if (present[b]) { maxBand = b; break; }

    // Wanted set.
    wanted_.clear();
    for (const CellRecord& c : cells) {
        if (!c.extentValid) continue;
        const bool bandOk = (c.band == 0) || (c.band >= 1 && c.band <= maxBand);
        if (!bandOk) continue;
        const double off = wrapOffsetFor((c.bbox.minx + c.bbox.maxx) / 2.0);
        if (shiftX(c.bbox, off).intersects(wantedArea)) wanted_.insert(c.path);
    }

    // Bring in newly-wanted cells. Each is built clipped in its own (real) frame;
    // the wrap offset is recorded so it can be drawn on the correct side of the
    // seam. keepArea shifted by -off puts the clip region into the cell's frame.
    for (const QString& path : wanted_) {
        if (loaded_.contains(path) || inFlight_.contains(path) ||
            building_.contains(path)) continue;
        const BBox bbox = bboxByPath_.value(path);
        const double off = wrapOffsetFor((bbox.minx + bbox.maxx) / 2.0);
        if (FeatureCache::FeaturesPtr feats = cache_.get(path))
            dispatchBuild(path, feats, bandByPath_.value(path, 0), shiftX(keepArea, -off), off);
        else
            dispatchLoad(path);
    }

    // Unload cells out of range; re-clip cells the view panned across (including
    // across the seam, where the offset — and thus the clip frame — changes).
    const QList<QString> loadedPaths = loaded_.keys();
    for (const QString& path : loadedPaths) {
        const int  band = bandByPath_.value(path, 0);
        const BBox bbox = bboxByPath_.value(path);
        const double off = wrapOffsetFor((bbox.minx + bbox.maxx) / 2.0);
        const bool bandOk = (band == 0) || (band >= 1 && band <= maxBand);
        const bool keep = bbox.valid() && bandOk && shiftX(bbox, off).intersects(keepArea);
        if (!keep) { removeCell(path); continue; }

        const BBox clipBox = loaded_[path].clipBox;           // real frame
        const BBox wantedRealFrame = shiftX(wantedArea, -off);
        if (!clipBox.contains(wantedRealFrame) && !building_.contains(path)) {
            if (FeatureCache::FeaturesPtr feats = cache_.get(path))
                dispatchBuild(path, feats, band, shiftX(keepArea, -off), off);
        }
    }

    emit statusChanged(QStringLiteral("Bands ≤ %1  ·  %2 cell(s) shown")
                           .arg(maxBand).arg(loaded_.size()));
    update();
}

// ---- async load / build ----------------------------------------------------

void ChartView::dispatchLoad(const QString& path) {
    inFlight_.insert(path);
    const quint64 gen = generation_;
    const std::string p = path.toStdString();

    auto* watcher = new QFutureWatcher<CellLoadResult>(this);
    connect(watcher, &QFutureWatcher<CellLoadResult>::finished, this, [this, watcher, gen]() {
        CellLoadResult r = watcher->result();
        watcher->deleteLater();
        onCellLoaded(std::move(r), gen);
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [p]() {
        CellLoadResult r;
        r.path = QString::fromStdString(p);
        std::string err;
        r.ok = chart::loadCellFeatures(p, r.features, r.bbox, err);
        if (!r.ok) r.error = QString::fromStdString(err);
        return r;
    }));
}

void ChartView::onCellLoaded(CellLoadResult r, quint64 gen) {
    if (gen != generation_) return;
    inFlight_.remove(r.path);
    if (!r.ok) return;

    const QString path = r.path;
    const int     band = bandByPath_.value(path, 0);

    auto feats = std::make_shared<std::vector<Feature>>(std::move(r.features));
    cache_.put(path, feats, approxBytes(*feats));

    if (!wanted_.contains(path)) return;
    if (loaded_.contains(path)) return;

    BBox view, wantedArea, keepArea;
    int target;
    if (!computeViewBoxes(view, wantedArea, keepArea, target)) return;
    const BBox bbox = bboxByPath_.value(path);
    const double off = wrapOffsetFor((bbox.minx + bbox.maxx) / 2.0);
    dispatchBuild(path, feats, band, shiftX(keepArea, -off), off);
}

void ChartView::dispatchBuild(const QString& path, FeatureCache::FeaturesPtr feats,
                              int band, const BBox& clipBox, double drawOffsetX) {
    if (!feats || building_.contains(path)) return;
    building_.insert(path);
    const quint64 gen = generation_;
    const QString p = path;

    auto* watcher = new QFutureWatcher<BuiltCell>(this);
    connect(watcher, &QFutureWatcher<BuiltCell>::finished, this,
            [this, watcher, gen, drawOffsetX]() {
        BuiltCell bc = watcher->result();
        watcher->deleteLater();
        onCellBuilt(std::move(bc), gen, drawOffsetX);
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [p, feats, band, clipBox]() {
        return buildCell(p, *feats, band, clipBox, simplifyToleranceM(band));
    }));
}

void ChartView::onCellBuilt(BuiltCell bc, quint64 gen, double drawOffsetX) {
    building_.remove(bc.path);
    if (gen != generation_) return;
    if (!wanted_.contains(bc.path)) return;
    bc.drawOffsetX = drawOffsetX;
    storeCell(std::move(bc));
    emit statusChanged(QStringLiteral("%1 cell(s) shown").arg(loaded_.size()));
    update();
}

void ChartView::storeCell(BuiltCell bc) {
    loaded_.insert(bc.path, std::move(bc));   // replaces any existing
}

void ChartView::removeCell(const QString& path) {
    loaded_.remove(path);
}

void ChartView::updatePointLOD() {
    if (ppm_ <= 0.0) return;
    const double visWidthM = width() / ppm_;
    const bool show = visWidthM < 20000.0;   // ~20 km across
    if (show == pointLodVisible_) return;
    pointLodVisible_ = show;
    update();
}

// ---- basemap (GSHHG land/lakes underlay) ----------------------------------

void ChartView::setBasemapDirectory(const QString& dir) {
    basemapDir_ = dir;
    reloadBasemap();
}

void ChartView::reloadBasemap() {
    const BasemapSource src = resolveBasemap(basemapDir_);
    basemapRoot_     = src.root;
    availableTiers_  = src.tiers;
    tierCache_.clear();
    basemapFeats_.reset();
    basemapTier_.clear();
    tierLoading_.clear();
    basemap_.clear();
    basemapClipBox_ = BBox();
    basemapBuiltPpm_ = 0.0;
    if (availableTiers_.isEmpty()) { update(); return; }
    ensureViewForBasemap();   // need a zoom to choose a tier
    ensureTierForZoom();      // load the tier appropriate for the current zoom
    update();
}

// With a basemap but no charts, establish a whole-world view so the underlay is
// visible on its own. A subsequent catalog load will fit to the charts instead.
void ChartView::ensureViewForBasemap() {
    if (ppm_ > 0.0 || availableTiers_.isEmpty() || width() <= 0 || height() <= 0) return;
    const double W = proj::lonToX(180.0);
    ppm_ = std::min(width() / (2.0 * W), height() / (2.0 * W));
    scx_ = 0.0; scy_ = 0.0;
    updatePointLOD();
}

// The GSHHG tier whose nominal resolution best matches the current zoom (metres
// per pixel), clamped to the tiers actually installed: coarse when zoomed out,
// fine when zoomed in. Prefers the ideal tier, then a coarser one, then a finer.
QString ChartView::desiredTier() const {
    if (availableTiers_.isEmpty()) return QString();
    const double mpp = (ppm_ > 0.0) ? 1.0 / ppm_ : 1e9;   // metres per pixel
    QString ideal;
    if      (mpp <=   120.0) ideal = QStringLiteral("f");   // full   (~50 m)
    else if (mpp <=   600.0) ideal = QStringLiteral("h");   // high   (~200 m)
    else if (mpp <=  3000.0) ideal = QStringLiteral("i");   // interm.(~1 km)
    else if (mpp <= 15000.0) ideal = QStringLiteral("l");   // low    (~5 km)
    else                     ideal = QStringLiteral("c");   // crude  (~25 km)

    static const QStringList order = {"f", "h", "i", "l", "c"};   // fine -> coarse
    int idx = order.indexOf(ideal);
    if (idx < 0) idx = order.size() - 1;
    for (int j = idx; j < order.size(); ++j)        // ideal, then coarser
        if (availableTiers_.contains(order[j])) return order[j];
    for (int j = idx - 1; j >= 0; --j)              // else the nearest finer
        if (availableTiers_.contains(order[j])) return order[j];
    return availableTiers_.first();
}

void ChartView::ensureTierForZoom() {
    if (availableTiers_.isEmpty() || ppm_ <= 0.0) return;
    const QString want = desiredTier();
    if (want.isEmpty() || want == basemapTier_) return;

    if (FeatureCache::FeaturesPtr cached = tierCache_.get(want)) {
        basemapFeats_   = cached;     // switch instantly; old stays drawn until rebuilt
        basemapTier_    = want;
        basemapBuiltPpm_ = 0.0;       // force a rebuild at the new tier
        tierCache_.trim();            // active tier now pinned; shed others over budget
        return;
    }
    if (tierLoading_ == want) return; // already loading this one
    loadTier(want);
}

void ChartView::loadTier(const QString& tier) {
    if (basemapRoot_.isEmpty()) return;
    tierLoading_ = tier;
    const std::string root = basemapRoot_.toStdString();
    const std::string t = tier.toStdString();

    auto* watcher = new QFutureWatcher<FeatureCache::FeaturesPtr>(this);
    connect(watcher, &QFutureWatcher<FeatureCache::FeaturesPtr>::finished, this,
            [this, watcher, tier]() {
        FeatureCache::FeaturesPtr feats = watcher->result();
        watcher->deleteLater();
        onTierLoaded(feats, tier);
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [root, t]() -> FeatureCache::FeaturesPtr {
        auto feats = std::make_shared<std::vector<Feature>>();
        std::string err;
        chart::loadBasemap(root, t, *feats, err);
        return feats;   // empty on failure
    }));
}

void ChartView::onTierLoaded(FeatureCache::FeaturesPtr feats, const QString& tier) {
    if (tierLoading_ == tier) tierLoading_.clear();
    if (!feats || feats->empty()) return;        // keep whatever we had
    tierCache_.put(tier, feats, approxBytes(*feats));
    if (desiredTier() == tier) {                 // still the right tier for this zoom
        basemapFeats_    = feats;
        basemapTier_     = tier;
        basemapBuiltPpm_ = 0.0;
        tierCache_.trim();                       // active pinned; shed others over budget
        maybeBuildBasemap();
    }
}

void ChartView::maybeBuildBasemap() {
    if (availableTiers_.isEmpty() || ppm_ <= 0.0) return;
    ensureTierForZoom();
    if (!basemapFeats_ || basemapBuilding_) return;

    BBox view, wantedArea, keepArea;
    int target;
    if (!computeViewBoxes(view, wantedArea, keepArea, target)) return;

    const bool zoomStale = basemapBuiltPpm_ <= 0.0 ||
        ppm_ > basemapBuiltPpm_ * 1.6 || ppm_ < basemapBuiltPpm_ * 0.6;
    if (!basemap_.empty() && !zoomStale && basemapClipBox_.contains(wantedArea))
        return;

    const double ww  = worldWidthM();
    const double W   = proj::lonToX(180.0);
    const double tol = 0.5 / ppm_;          // ~half a pixel, zoom-appropriate

    // Whole-world copies the view spans (normally {0}; ±1 near the date line).
    int kmin = static_cast<int>(std::ceil((keepArea.minx - W) / ww));
    int kmax = static_cast<int>(std::floor((keepArea.maxx + W) / ww));
    kmin = std::max(kmin, -2); kmax = std::min(kmax, 2);

    std::vector<std::pair<BBox, double>> reqs;   // (clip box real frame, offset)
    for (int k = kmin; k <= kmax; ++k)
        reqs.emplace_back(shiftX(keepArea, -k * ww), k * ww);
    if (reqs.empty()) return;

    basemapBuilding_ = true;
    basemapClipBox_  = keepArea;
    basemapBuiltPpm_ = ppm_;
    auto feats = basemapFeats_;

    auto* watcher = new QFutureWatcher<std::vector<BuiltCell>>(this);
    connect(watcher, &QFutureWatcher<std::vector<BuiltCell>>::finished, this,
            [this, watcher, feats]() {
        std::vector<BuiltCell> cells = watcher->result();
        watcher->deleteLater();
        onBasemapBuilt(std::move(cells), feats);
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [feats, reqs, tol]() {
        std::vector<BuiltCell> result;
        result.reserve(reqs.size());
        for (const auto& r : reqs) {
            BuiltCell bc = buildCell(QString(), *feats, 0, r.first, tol);
            bc.drawOffsetX = r.second;
            result.push_back(std::move(bc));
        }
        return result;
    }));
}

void ChartView::onBasemapBuilt(std::vector<BuiltCell> cells, FeatureCache::FeaturesPtr feats) {
    basemapBuilding_ = false;
    if (feats != basemapFeats_) return;     // data was reloaded while building
    basemap_ = std::move(cells);
    update();
}

void ChartView::setShowSoundings(bool on) {
    if (on == showSoundings_) return;
    showSoundings_ = on; update();
}
void ChartView::setShowSymbols(bool on) {
    if (on == showSymbols_) return;
    showSymbols_ = on; update();
}
void ChartView::setShowDepthContours(bool on) {
    if (on == showDepthContours_) return;
    showDepthContours_ = on; update();
}

void ChartView::setInitialView(double lon, double lat, double scale) {
    pendingLon_ = lon; pendingLat_ = lat; pendingScale_ = scale;
    havePendingView_ = (scale > 0.0);
}

void ChartView::persistViewNow() {
    double lon, lat, scale;
    if (currentView(lon, lat, scale)) emit viewChanged(lon, lat, scale);
}

void ChartView::beginInteraction() {
    interacting_ = true;
    aaTimer_->start();
    saveTimer_->start();
}

// ---- painting --------------------------------------------------------------

void ChartView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(204, 224, 242));

    if (ppm_ <= 0.0) {     // no view established yet (no charts, no basemap)
        p.setPen(QColor(80, 80, 80));
        QFont f = p.font(); f.setPointSize(13); p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("Tap the menu button (top-left) to open a chart folder."));
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, !interacting_);

    const QTransform cam = cameraTransform();
    const QRectF vis = QRectF(scx_ - (width()  / 2.0) / ppm_,
                              scy_ - (height() / 2.0) / ppm_,
                              width() / ppm_, height() / ppm_);

    QPen pen; pen.setCosmetic(true);
    // Draw one cell's vector geometry, shifted by its wrap offset.
    auto drawPaths = [&](const BuiltCell& c) {
        const double off = c.drawOffsetX;
        QTransform t = cam;
        if (off != 0.0) t.translate(off, 0.0);
        p.setTransform(t);
        const QRectF visFrame = vis.translated(-off, 0.0);   // cull in cell frame
        for (const BuiltPath& bp : c.paths) {
            if (bp.isDepthContour && !showDepthContours_) continue;
            if (!bp.bounds.intersects(visFrame)) continue;
            p.setBrush(bp.filled ? QBrush(bp.brush) : QBrush(Qt::NoBrush));
            if (bp.hasPen) { pen.setColor(bp.penColor); pen.setWidthF(bp.penWidth); p.setPen(pen); }
            else           { p.setPen(Qt::NoPen); }
            p.drawPath(bp.path);
        }
    };

    // 0) Basemap underlay (land/lakes) beneath everything; charts cover it where
    //    they exist.
    for (const BuiltCell& bc : basemap_) drawPaths(bc);

    // 1) Chart cells, coarser bands first so finer detail draws on top.
    std::vector<const BuiltCell*> order;
    order.reserve(loaded_.size());
    for (auto it = loaded_.constBegin(); it != loaded_.constEnd(); ++it)
        order.push_back(&it.value());
    std::sort(order.begin(), order.end(),
              [](const BuiltCell* a, const BuiltCell* b) { return a->band < b->band; });
    for (const BuiltCell* c : order) drawPaths(*c);

    // 2) Soundings / symbols at constant on-screen size, in device space.
    p.resetTransform();
    if (pointLodVisible_) {
        const QRectF screen = rect().adjusted(-24, -24, 24, 24);
        if (showSoundings_) {
            QFont f = p.font(); f.setPointSizeF(8.0); p.setFont(f);
            p.setPen(QColor(26, 51, 115));
            const double asc = QFontMetricsF(f).ascent();
            for (const BuiltCell* c : order) {
                const double off = c->drawOffsetX;
                for (const auto& s : c->soundings) {
                    const QPointF d = cam.map(QPointF(s.first.x() + off, s.first.y()));
                    if (!screen.contains(d)) continue;
                    p.drawText(QPointF(d.x() + 1.0, d.y() + asc), s.second);
                }
            }
        }
        if (showSymbols_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(179, 26, 128));
            for (const BuiltCell* c : order) {
                const double off = c->drawOffsetX;
                for (const QPointF& q : c->symbols) {
                    const QPointF d = cam.map(QPointF(q.x() + off, q.y()));
                    if (!screen.contains(d)) continue;
                    p.drawEllipse(d, 3.0, 3.0);
                }
            }
        }
    }

    // 3) Ownship overlay (top of stack).
    drawOwnship(p, cam);
}

void ChartView::setOwnship(const OwnshipState& s, NavFreshness f) {
    ownship_ = s;
    ownshipFreshness_ = f;
    update();
}

void ChartView::drawOwnship(QPainter& p, const QTransform& cam) {
    if (ownshipFreshness_ == NavFreshness::Invalid) return;
    if (!ownship_.latitudeDeg.has_value() || !ownship_.longitudeDeg.has_value()) return;

    // Project ownship into the scene, then to the nearest world copy so it shows
    // on-screen even when the user has wrapped across the date line.
    const double sx = proj::lonToX(*ownship_.longitudeDeg);
    const double sy = -proj::latToY(*ownship_.latitudeDeg);
    const double off = wrapOffsetFor(sx);
    const QPointF d = cam.map(QPointF(sx + off, sy));

    // Heading for the triangle: prefer true heading, fall back to COG.
    double headingDeg = 0.0;
    bool   haveHeading = false;
    if (ownship_.headingDegTrue.has_value()) {
        headingDeg = *ownship_.headingDegTrue; haveHeading = true;
    } else if (ownship_.cogDegTrue.has_value()) {
        headingDeg = *ownship_.cogDegTrue;     haveHeading = true;
    }

    p.save();
    p.resetTransform();                   // draw in device pixels, constant size
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(d);
    if (haveHeading) p.rotate(headingDeg);

    // A 1-minute predictor line ahead (length scaled by SOG, capped). Drawn
    // before the triangle so the symbol sits on top.
    if (ownship_.sogKnots.value_or(0.0) > 0.1) {
        const double mppx = (ppm_ > 0.0) ? 1.0 / ppm_ : 0.0;
        const double dist_m = (*ownship_.sogKnots) * (1852.0 / 60.0);   // 1 min
        double len = (mppx > 0.0) ? dist_m / mppx : 0.0;
        len = std::clamp(len, 12.0, 120.0);
        QPen line(QColor(20, 20, 20, 220)); line.setWidthF(1.5);
        p.setPen(line);
        p.drawLine(QPointF(0, 0), QPointF(0, -len));
    }

    // Triangle pointing along heading (or COG fallback). Dimmer when stale.
    QPolygonF tri;
    tri << QPointF(0, -14) << QPointF(8, 8) << QPointF(-8, 8);
    const bool stale = (ownshipFreshness_ == NavFreshness::Stale);
    QColor fill = stale ? QColor(200, 110, 110, 200) : QColor(220, 30, 30);
    QPen edge(QColor(40, 0, 0)); edge.setWidthF(1.2);
    p.setBrush(fill);
    p.setPen(edge);
    if (haveHeading) {
        p.drawPolygon(tri);
    } else {
        // No heading at all: render a circle so the user sees position without
        // implying a direction we don't have.
        p.drawEllipse(QPointF(0, 0), 7.0, 7.0);
    }

    if (stale) {
        // Cancellation slash through the position: a short black line at right
        // angles to the centerline, following the marine convention for an
        // invalid / DR (dead-reckoned) fix.
        QPen slash(QColor(0, 0, 0)); slash.setWidthF(1.6);
        p.setPen(slash);
        p.drawLine(QPointF(-8.0, 0.0), QPointF(8.0, 0.0));
    }
    p.restore();
}

// ---- input -----------------------------------------------------------------

void ChartView::wheelEvent(QWheelEvent* e) {
    if (ppm_ <= 0.0) { e->ignore(); return; }
    userInteracted_ = true;
    const double step = 1.15;
    const double factor = (e->angleDelta().y() > 0) ? step : 1.0 / step;
    const double target = ppm_ * factor;
    if (target < 1e-9 || target > 1e6) { e->accept(); return; }

    // Keep the scene point under the cursor fixed.
    const QPointF cur = e->position();
    const QPointF under = screenToScene(cur);
    ppm_ = target;
    scx_ = under.x() - (cur.x() - width() / 2.0) / ppm_;
    scy_ = under.y() - (cur.y() - height() / 2.0) / ppm_;
    normalizeCenter();

    beginInteraction();
    updatePointLOD();
    scheduleUpdate();
    update();
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && ppm_ > 0.0) {
        dragging_ = true;
        lastDragPos_ = e->position();
        userInteracted_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(e);
}

void ChartView::mouseMoveEvent(QMouseEvent* e) {
    if (ppm_ > 0.0) {
        const QPointF s = screenToScene(e->position());
        emit cursorMoved(proj::xToLon(s.x()), proj::yToLat(-s.y()));
        if (dragging_) {
            const QPointF d = e->position() - lastDragPos_;
            lastDragPos_ = e->position();
            scx_ -= d.x() / ppm_;
            scy_ -= d.y() / ppm_;
            normalizeCenter();
            beginInteraction();
            scheduleUpdate();
            update();
        }
    }
    QWidget::mouseMoveEvent(e);
}

void ChartView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
    }
    QWidget::mouseReleaseEvent(e);
}

void ChartView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (haveCatalog_ && !userInteracted_) fitToCatalog();
    else { updatePointLOD(); scheduleUpdate(); }
    ensureViewForBasemap();   // in case the widget had no size when basemap loaded
    maybeBuildBasemap();
    update();
}
