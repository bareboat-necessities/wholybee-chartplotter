#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "projection.hpp"
#include "geom_clip.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsItem>
#include <QStyleOptionGraphicsItem>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetricsF>
#include <QTransform>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <climits>

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

// Per-region geometry clipping lives in geom_clip.hpp (pure, unit-tested). We
// pull the three entry points we use into this scope. Cells are cached fully
// (viewport-independent); at scene-build time we clip to a region a little
// larger than the view, so a coarse cell contributing only a gap-fill sliver
// carries a screen-sized polygon into the scene instead of a basin-spanning one
// — Qt then traverses and rasterizes far less per frame. The clip region is
// always larger than the viewport, so the edges clipping introduces fall
// off-screen.
using geom::clipRingToRect;
using geom::clipPolylineToRect;
using geom::pointInRect;

// All of a cell's soundings (or point symbols) drawn by a single item at a
// constant on-screen size. This replaces one QGraphicsItem per point — and,
// crucially, the per-item ItemIgnoresTransformations flag, which is a major
// QGraphicsView bottleneck because such items bypass spatial culling and are
// re-transformed individually every frame. Here we draw directly in device
// pixels (one paint pass, with our own viewport cull), so a busy harbour view
// costs two items instead of thousands.
class ConstantSizePoints : public QGraphicsItem {
public:
    enum Kind { Soundings, Symbols };
    explicit ConstantSizePoints(Kind k) : kind_(k) {}

    // Position is given in scene coordinates (caller has already flipped Y).
    void add(double sceneX, double sceneY, const QString& text = QString()) {
        pts_.push_back({QPointF(sceneX, sceneY), text});
        bounds_ |= QRectF(sceneX, sceneY, 0.0001, 0.0001);
    }
    bool empty() const { return pts_.empty(); }

    QRectF boundingRect() const override { return bounds_; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget*) override {
        const QTransform t = p->worldTransform();   // scene -> device
        const double sx = std::abs(t.m11()) > 1e-12 ? std::abs(t.m11()) : 1.0;
        // Cull in scene coords, padded so constant-size glyphs near the edge of
        // the exposed region aren't dropped.
        const double pad = 32.0 / sx;
        const QRectF cull = opt->exposedRect.adjusted(-pad, -pad, pad, pad);

        p->save();
        p->setWorldMatrixEnabled(false);             // draw in device pixels
        if (kind_ == Soundings) {
            QFont f = p->font(); f.setPointSizeF(8.0); p->setFont(f);
            p->setPen(QColor(26, 51, 115));
            const double asc = QFontMetricsF(f).ascent();
            for (const E& e : pts_) {
                if (!cull.contains(e.pos)) continue;
                const QPointF d = t.map(e.pos);
                p->drawText(QPointF(d.x() + 1.0, d.y() + asc), e.text);
            }
        } else {
            p->setBrush(QColor(179, 26, 128));
            p->setPen(Qt::NoPen);
            for (const E& e : pts_) {
                if (!cull.contains(e.pos)) continue;
                const QPointF d = t.map(e.pos);
                p->drawEllipse(d, 3.0, 3.0);
            }
        }
        p->restore();
    }

private:
    struct E { QPointF pos; QString text; };
    Kind kind_;
    QRectF bounds_;
    std::vector<E> pts_;
};

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
// the band's display scale on a typical screen. Band-based rather than live-zoom
// based, so geometry never needs re-simplifying as you zoom within a band — and
// the finest bands (berthing/unknown), where the user zooms in most, keep full
// detail. Bands: 1 overview … 5 harbour.
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

// Clip + simplify one cell to `clipBox` and build its vector primitives. Pure
// and Qt-value-only, so it runs on a worker thread; the UI thread later wraps
// the result in QGraphicsItems (see ChartView::instantiateCell).
BuiltCell buildCell(const QString& path, const std::vector<Feature>& feats,
                    int band, const BBox& clipBox) {
    BuiltCell bc;
    bc.path    = path;
    bc.band    = band;
    bc.clipBox = clipBox;

    // Band-major depth: coarser bands sit beneath finer ones. Feature z-order
    // orders within a band.
    const double zb  = static_cast<double>(band) * 1000.0;
    const double tol = simplifyToleranceM(band);

    std::vector<std::vector<Pt>> clipBuf;   // clipped rings/runs
    std::vector<std::vector<Pt>> simpBuf;   // simplified rings/runs (reused)

    // Reduce a set of rings/runs by the band tolerance, keeping only those still
    // large enough to draw. Returns the buffer to build a path from.
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
    return bc;
}

} // namespace

ChartView::ChartView(QWidget* parent) : QGraphicsView(parent) {
    setScene(&scene_);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    viewport()->setMouseTracking(true);

    pool_.setMaxThreadCount(qBound(2, QThread::idealThreadCount(), 8));

    // LRU of parsed cells. Currently-loaded (on-screen) cells are pinned so a
    // tight budget never evicts geometry that's visible.
    cache_.setLimits(256u * 1024u * 1024u, 256);   // ~256 MB, 256 cells
    cache_.setPinned([this](const QString& path) { return loaded_.contains(path); });

    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(120); // debounce viewport changes
    connect(updateTimer_, &QTimer::timeout, this, &ChartView::updateVisibleCells);

    // While the user is actively panning/zooming we drop antialiasing for speed,
    // then restore it shortly after movement stops for a crisp static image.
    aaTimer_ = new QTimer(this);
    aaTimer_->setSingleShot(true);
    aaTimer_->setInterval(180);
    connect(aaTimer_, &QTimer::timeout, this, [this] {
        setRenderHint(QPainter::Antialiasing, true);
        viewport()->update();
    });

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &ChartView::scheduleUpdate);
    connect(verticalScrollBar(),   &QScrollBar::valueChanged, this, &ChartView::scheduleUpdate);
}

void ChartView::setCatalog(ChartCatalog* catalog) {
    catalog_ = catalog;
    if (catalog_)
        connect(catalog_, &ChartCatalog::finished, this, &ChartView::onCatalogFinished);
}

void ChartView::clearAll() {
    scene_.clear();
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
    ++generation_;          // invalidate any in-flight loads from a previous scan
    clearAll();
    haveCatalog_ = ok && catalog_ && catalog_->bounds().valid();
    userInteracted_ = false;

    if (!haveCatalog_) {
        scene_.setSceneRect(QRectF());
        viewport()->update();
        emit statusChanged(QString());
        return;
    }

    const BBox& b = catalog_->bounds();
    scene_.setSceneRect(b.minx, -b.maxy, b.maxx - b.minx, b.maxy - b.miny);
    bandByPath_.reserve(static_cast<int>(catalog_->cells().size()));
    bboxByPath_.reserve(static_cast<int>(catalog_->cells().size()));
    for (const CellRecord& c : catalog_->cells()) {
        bandByPath_.insert(c.path, c.band);
        if (c.extentValid) bboxByPath_.insert(c.path, c.bbox);
    }
    fitToCatalog();         // triggers updateVisibleCells via scrollbar/explicit call
    updateVisibleCells();
}

void ChartView::fitToCatalog() {
    if (!haveCatalog_) return;
    QRectF r = scene_.sceneRect();
    double mx = r.width() * 0.04, my = r.height() * 0.04;
    fitInView(r.adjusted(-mx, -my, mx, my), Qt::KeepAspectRatio);
    updatePointLOD();
}

// ---- viewport-driven selection -------------------------------------------

int ChartView::bandForVisibleWidth(double metres) {
    const double nm = metres / 1852.0;
    if (nm > 1500.0) return 1; // overview
    if (nm >  300.0) return 2; // general
    if (nm >   50.0) return 3; // coastal
    if (nm >   15.0) return 4; // approach
    if (nm >    3.0) return 5; // harbour
    return 6;                  // berthing
}

BBox ChartView::expandBox(const BBox& b, double frac) {
    double dx = (b.maxx - b.minx) * frac;
    double dy = (b.maxy - b.miny) * frac;
    BBox r;
    r.minx = b.minx - dx; r.maxx = b.maxx + dx;
    r.miny = b.miny - dy; r.maxy = b.maxy + dy;
    return r;
}

void ChartView::scheduleUpdate() {
    if (haveCatalog_) updateTimer_->start();
}

bool ChartView::computeViewBoxes(BBox& view, BBox& wanted, BBox& keep, int& target) const {
    const double pxPerMeter = transform().m11();
    if (pxPerMeter <= 0.0) return false;

    const QRectF sr = mapToScene(viewport()->rect()).boundingRect();
    view.minx = sr.left();    view.maxx = sr.right();
    view.miny = -sr.bottom(); view.maxy = -sr.top();    // scene Y is flipped

    const double visWidthM = viewport()->width() / pxPerMeter;
    target = bandForVisibleWidth(visWidthM);

    wanted = expandBox(view, 0.5);   // load (and re-clip trigger) just beyond the edge
    keep   = expandBox(view, 1.5);   // clip region + unload only well beyond it
    return true;
}

void ChartView::updateVisibleCells() {
    if (!haveCatalog_ || !catalog_) return;

    BBox view, wantedArea, keepArea;
    int target;
    if (!computeViewBoxes(view, wantedArea, keepArea, target)) return;

    const std::vector<CellRecord>& cells = catalog_->cells();

    // Which bands have coverage in view?
    bool present[7] = {false,false,false,false,false,false,false};
    for (const CellRecord& c : cells) {
        if (!c.extentValid || c.band < 1 || c.band > 6) continue;
        if (c.bbox.intersects(wantedArea)) present[c.band] = true;
    }

    // Gap-fill quilting: show the zoom-appropriate band on top, plus every
    // coarser available band beneath it to fill areas the finer band doesn't
    // cover. So we display all present bands in [1 .. maxBand].
    //
    // maxBand is normally the zoom target. If no band at/below target has any
    // coverage here (only finer data exists), fall back to the coarsest band
    // finer than the target so the screen isn't blank.
    int maxBand = target;
    bool haveAtOrBelow = false;
    for (int b = 1; b <= target; ++b) if (present[b]) { haveAtOrBelow = true; break; }
    if (!haveAtOrBelow) {
        for (int b = target + 1; b <= 6; ++b) if (present[b]) { maxBand = b; break; }
    }

    // Wanted set: every band from 1..maxBand (plus unknown band) inside wantedArea.
    wanted_.clear();
    for (const CellRecord& c : cells) {
        if (!c.extentValid) continue;
        const bool bandOk = (c.band == 0) || (c.band >= 1 && c.band <= maxBand);
        if (!bandOk) continue;
        if (c.bbox.intersects(wantedArea)) wanted_.insert(c.path);
    }

    // Bring in newly-wanted cells. A cache hit dispatches a (fast) off-thread
    // build from the cached parse; a miss dispatches a disk+GDAL load, which then
    // dispatches the build. Either way the UI thread never clips or builds paths.
    for (const QString& path : wanted_) {
        if (loaded_.contains(path) || inFlight_.contains(path) ||
            building_.contains(path)) continue;
        if (FeatureCache::FeaturesPtr feats = cache_.get(path))
            dispatchBuild(path, feats, bandByPath_.value(path, 0), keepArea);
        else
            dispatchLoad(path);
    }

    // Unload cells outside keepArea or whose band is now finer than maxBand;
    // re-clip cells the view has panned/zoomed across so their clipped geometry
    // keeps covering the visible area.
    const QList<QString> loadedPaths = loaded_.keys();
    for (const QString& path : loadedPaths) {
        const int  band = bandByPath_.value(path, 0);
        const BBox bbox = bboxByPath_.value(path);
        const bool bandOk = (band == 0) || (band >= 1 && band <= maxBand);
        const bool keep = bbox.valid() && bandOk && bbox.intersects(keepArea);
        if (!keep) { removeCell(path); continue; }

        // wantedArea sits a full viewport-width inside keepArea, so triggering on
        // it re-clips with margin to spare — the visible viewport never reaches
        // the edge of the old clip, so no blank sliver can appear.
        const BBox clipBox = loaded_[path].clipBox;
        if (!clipBox.contains(wantedArea) && !building_.contains(path)) {
            if (FeatureCache::FeaturesPtr feats = cache_.get(path))
                dispatchBuild(path, feats, band, keepArea);
        }
    }

    emit statusChanged(QStringLiteral("Bands \u2264 %1  \u00b7  %2 cell(s) shown")
                           .arg(maxBand).arg(loaded_.size()));
}

// ---- async load / scene management ---------------------------------------

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
    if (gen != generation_) return;            // result from a superseded scan
    inFlight_.remove(r.path);
    if (!r.ok) return;

    const QString path = r.path;
    const int     band = bandByPath_.value(path, 0);

    // Cache the parse regardless of whether we still want it on screen — a quick
    // return to this area should then be instant (rebuilt from cache, no reload).
    auto feats = std::make_shared<std::vector<Feature>>(std::move(r.features));
    cache_.put(path, feats, approxBytes(*feats));

    if (!wanted_.contains(path)) return;       // view moved on; keep only in cache
    if (loaded_.contains(path)) return;        // already present

    BBox view, wantedArea, keepArea;
    int target;
    if (!computeViewBoxes(view, wantedArea, keepArea, target)) return;
    dispatchBuild(path, feats, band, keepArea);
}

void ChartView::dispatchBuild(const QString& path, FeatureCache::FeaturesPtr feats,
                             int band, const BBox& clipBox) {
    if (!feats || building_.contains(path)) return;   // one build per cell at a time
    building_.insert(path);
    const quint64 gen = generation_;
    const QString p = path;

    auto* watcher = new QFutureWatcher<BuiltCell>(this);
    connect(watcher, &QFutureWatcher<BuiltCell>::finished, this, [this, watcher, gen]() {
        BuiltCell bc = watcher->result();
        watcher->deleteLater();
        onCellBuilt(std::move(bc), gen);
    });
    watcher->setFuture(QtConcurrent::run(&pool_, [p, feats, band, clipBox]() {
        return buildCell(p, *feats, band, clipBox);
    }));
}

void ChartView::onCellBuilt(BuiltCell bc, quint64 gen) {
    building_.remove(bc.path);
    if (gen != generation_) return;            // superseded by a new scan
    if (!wanted_.contains(bc.path)) return;    // view moved on while building
    instantiateCell(bc);
    emit statusChanged(QStringLiteral("%1 cell(s) shown").arg(loaded_.size()));
}

void ChartView::instantiateCell(const BuiltCell& bc) {
    LoadedCell cell;
    cell.band    = bc.band;
    cell.clipBox = bc.clipBox;

    for (const BuiltPath& bp : bc.paths) {
        auto* item = new QGraphicsPathItem(bp.path);
        item->setBrush(bp.filled ? QBrush(bp.brush) : QBrush(Qt::NoBrush));
        if (bp.hasPen) {
            QPen pen(bp.penColor); pen.setCosmetic(true); pen.setWidthF(bp.penWidth);
            item->setPen(pen);
        } else {
            item->setPen(Qt::NoPen);
        }
        item->setZValue(bp.z);
        scene_.addItem(item);
        cell.items.push_back(item);
        if (bp.isDepthContour) {
            item->setVisible(contourVisible());
            cell.contours.push_back(item);
        }
    }

    // Collapse all soundings / symbols into one constant-size item each.
    const double zb = static_cast<double>(bc.band) * 1000.0;
    if (!bc.soundings.empty()) {
        auto* it = new ConstantSizePoints(ConstantSizePoints::Soundings);
        for (const auto& s : bc.soundings) it->add(s.first.x(), s.first.y(), s.second);
        it->setZValue(zb + 900.0);
        it->setVisible(soundingVisible());
        scene_.addItem(it);
        cell.items.push_back(it);
        cell.soundings.push_back(it);
    }
    if (!bc.symbols.empty()) {
        auto* it = new ConstantSizePoints(ConstantSizePoints::Symbols);
        for (const QPointF& q : bc.symbols) it->add(q.x(), q.y());
        it->setZValue(zb + 901.0);
        it->setVisible(symbolVisible());
        scene_.addItem(it);
        cell.items.push_back(it);
        cell.symbols.push_back(it);
    }

    // Swap: the new items are already in the scene, so dropping the old ones now
    // never leaves a blank frame during a re-clip.
    removeCell(bc.path);
    loaded_.insert(bc.path, cell);
}

void ChartView::removeCell(const QString& path) {
    auto it = loaded_.find(path);
    if (it == loaded_.end()) return;
    for (QGraphicsItem* item : it->items)
        delete item;            // also removes from the scene
    loaded_.erase(it);
}

void ChartView::updatePointLOD() {
    const double pxPerMeter = transform().m11();
    if (pxPerMeter <= 0.0) return;
    const double visWidthM = viewport()->width() / pxPerMeter;
    const bool show = visWidthM < 20000.0; // ~20 km across
    if (show == pointLodVisible_) return;
    pointLodVisible_ = show;
    const bool sv = soundingVisible();
    const bool yv = symbolVisible();
    for (auto it = loaded_.begin(); it != loaded_.end(); ++it) {
        for (QGraphicsItem* p : it->soundings) p->setVisible(sv);
        for (QGraphicsItem* p : it->symbols)   p->setVisible(yv);
    }
}

void ChartView::setShowSoundings(bool on) {
    if (on == showSoundings_) return;
    showSoundings_ = on;
    const bool v = soundingVisible();
    for (auto it = loaded_.begin(); it != loaded_.end(); ++it)
        for (QGraphicsItem* s : it->soundings) s->setVisible(v);
}

void ChartView::setShowSymbols(bool on) {
    if (on == showSymbols_) return;
    showSymbols_ = on;
    const bool v = symbolVisible();
    for (auto it = loaded_.begin(); it != loaded_.end(); ++it)
        for (QGraphicsItem* s : it->symbols) s->setVisible(v);
}

void ChartView::setShowDepthContours(bool on) {
    if (on == showDepthContours_) return;
    showDepthContours_ = on;
    const bool v = contourVisible();
    for (auto it = loaded_.begin(); it != loaded_.end(); ++it)
        for (QGraphicsItem* c : it->contours) c->setVisible(v);
}

// ---- input ----------------------------------------------------------------

void ChartView::beginInteraction() {
    // Drop antialiasing for the duration of the gesture; the timer turns it back
    // on (and repaints crisply) shortly after the user stops moving.
    if (renderHints().testFlag(QPainter::Antialiasing)) {
        setRenderHint(QPainter::Antialiasing, false);
        viewport()->update();
    }
    aaTimer_->start();
}

void ChartView::wheelEvent(QWheelEvent* e) {
    if (!haveCatalog_) { QGraphicsView::wheelEvent(e); return; }
    userInteracted_ = true;
    const double step = 1.15;
    double factor = (e->angleDelta().y() > 0) ? step : 1.0 / step;
    double target = transform().m11() * factor;
    if (target < 1e-8 || target > 1e3) { e->accept(); return; }
    scale(factor, factor);
    beginInteraction();
    updatePointLOD();
    scheduleUpdate();
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
    userInteracted_ = true;
    QGraphicsView::mousePressEvent(e);
}

void ChartView::mouseMoveEvent(QMouseEvent* e) {
    if (haveCatalog_) {
        QPointF s = mapToScene(e->position().toPoint());
        emit cursorMoved(proj::xToLon(s.x()), proj::yToLat(-s.y()));
        if (e->buttons() & Qt::LeftButton) beginInteraction();   // dragging = pan
    }
    QGraphicsView::mouseMoveEvent(e);
}

void ChartView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (haveCatalog_ && !userInteracted_) fitToCatalog();
    else scheduleUpdate();
}

void ChartView::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, QColor(204, 224, 242));
}

void ChartView::drawForeground(QPainter* p, const QRectF&) {
    if (haveCatalog_) return;
    p->save();
    p->resetTransform();
    p->setPen(QColor(80, 80, 80));
    QFont f = p->font(); f.setPointSize(13); p->setFont(f);
    p->drawText(viewport()->rect(), Qt::AlignCenter,
                QStringLiteral("Tap the menu button (top-left) to open a chart folder."));
    p->restore();
}
