#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "projection.hpp"
#include "geom_clip.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsEllipseItem>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QPainter>
#include <QPen>
#include <QFont>
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

} // namespace

ChartView::ChartView(QWidget* parent) : QGraphicsView(parent) {
    setScene(&scene_);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
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

    // Bring in newly-wanted cells. A cache hit rebuilds the scene items straight
    // from the cached parse on the spot (no worker, no disk) — this is what
    // makes returning to a recently-seen area instant. A miss dispatches a load.
    for (const QString& path : wanted_) {
        if (loaded_.contains(path) || inFlight_.contains(path)) continue;
        if (FeatureCache::FeaturesPtr feats = cache_.get(path))
            addCell(path, *feats, bandByPath_.value(path, 0), keepArea);
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
        if (!clipBox.contains(wantedArea)) {
            if (FeatureCache::FeaturesPtr feats = cache_.get(path))
                addCell(path, *feats, band, keepArea);
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
    addCell(path, *feats, band, keepArea);
    emit statusChanged(QStringLiteral("%1 cell(s) shown").arg(loaded_.size()));
}

void ChartView::addCell(const QString& path, const std::vector<Feature>& feats,
                        int band, const BBox& clipBox) {
    // Rebuilding (re-clip, or cache hit while a stale copy somehow lingers):
    // drop any existing items for this path first.
    if (loaded_.contains(path)) removeCell(path);

    LoadedCell cell;
    cell.band    = band;
    cell.clipBox = clipBox;

    // Band-major depth: coarser bands sit beneath finer ones, so a finer cell's
    // opaque area fills cover the coarse data within its footprint, while gaps
    // reveal the coarser band underneath. Feature z-order orders within a band.
    const double zb = static_cast<double>(band) * 1000.0;

    // Scratch reused across features to avoid per-feature allocation churn.
    std::vector<std::vector<Pt>> ringsBuf;

    for (const Feature& f : feats) {
        // Skip clipping entirely when the feature is already inside the region —
        // the common case for fine cells smaller than the viewport.
        const bool doClip = clipBox.valid() && !clipBox.contains(f.bbox);

        switch (f.kind) {
            case FeatureKind::DepthArea:
            case FeatureKind::LandArea: {
                const std::vector<std::vector<Pt>>* rings = &f.rings;
                if (doClip) {
                    ringsBuf.clear();
                    for (const auto& ring : f.rings) {
                        std::vector<Pt> c = clipRingToRect(ring, clipBox);
                        if (c.size() >= 3) ringsBuf.push_back(std::move(c));
                    }
                    if (ringsBuf.empty()) break;       // nothing of this poly is in view
                    rings = &ringsBuf;
                }
                auto* item = new QGraphicsPathItem(buildPathFromRings(*rings, true));
                item->setBrush(fillColor(f));
                if (f.kind == FeatureKind::LandArea) {
                    QPen pen(QColor(115, 97, 64)); pen.setCosmetic(true); pen.setWidthF(1.0);
                    item->setPen(pen);
                } else {
                    item->setPen(Qt::NoPen);
                }
                item->setZValue(zb + f.zorder);
                scene_.addItem(item);
                cell.items.push_back(item);
                break;
            }
            case FeatureKind::OtherArea:
            case FeatureKind::DepthContour:
            case FeatureKind::Coastline:
            case FeatureKind::OtherLine: {
                const std::vector<std::vector<Pt>>* rings = &f.rings;
                if (doClip) {
                    ringsBuf.clear();
                    for (const auto& ring : f.rings) {
                        std::vector<std::vector<Pt>> runs = clipPolylineToRect(ring, clipBox);
                        for (auto& run : runs) ringsBuf.push_back(std::move(run));
                    }
                    if (ringsBuf.empty()) break;
                    rings = &ringsBuf;
                }
                auto* item = new QGraphicsPathItem(buildPathFromRings(*rings, false));
                item->setBrush(Qt::NoBrush);
                QPen pen; pen.setCosmetic(true);
                if (f.kind == FeatureKind::Coastline)       { pen.setColor(QColor(64, 51, 31));   pen.setWidthF(1.4); }
                else if (f.kind == FeatureKind::DepthContour){ pen.setColor(QColor(115, 153, 199)); pen.setWidthF(0.8); }
                else if (f.kind == FeatureKind::OtherArea)  { pen.setColor(QColor(102, 102, 115, 150)); pen.setWidthF(0.7); }
                else                                        { pen.setColor(QColor(102, 102, 128)); pen.setWidthF(0.8); }
                item->setPen(pen);
                item->setZValue(zb + f.zorder);
                scene_.addItem(item);
                cell.items.push_back(item);
                if (f.kind == FeatureKind::DepthContour) {
                    item->setVisible(contourVisible());
                    cell.contours.push_back(item);
                }
                break;
            }
            case FeatureKind::Sounding: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                if (doClip && !pointInRect(f.rings[0][0], clipBox)) break;
                QString text = f.hasDepth
                    ? QString::number(f.depth, 'f', f.depth < 10.0 ? 1 : 0)
                    : QStringLiteral(".");
                auto* t = new QGraphicsSimpleTextItem(text);
                QFont fnt = t->font(); fnt.setPointSizeF(8.0); t->setFont(fnt);
                t->setBrush(QColor(26, 51, 115));
                t->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                t->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                t->setZValue(zb + f.zorder);
                t->setVisible(soundingVisible());
                scene_.addItem(t);
                cell.items.push_back(t);
                cell.soundings.push_back(t);
                break;
            }
            case FeatureKind::Point: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                if (doClip && !pointInRect(f.rings[0][0], clipBox)) break;
                auto* e = new QGraphicsEllipseItem(-3.0, -3.0, 6.0, 6.0);
                e->setBrush(QColor(179, 26, 128));
                e->setPen(Qt::NoPen);
                e->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                e->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                e->setZValue(zb + f.zorder);
                e->setVisible(symbolVisible());
                scene_.addItem(e);
                cell.items.push_back(e);
                cell.symbols.push_back(e);
                break;
            }
        }
    }
    loaded_.insert(path, cell);
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

void ChartView::wheelEvent(QWheelEvent* e) {
    if (!haveCatalog_) { QGraphicsView::wheelEvent(e); return; }
    userInteracted_ = true;
    const double step = 1.15;
    double factor = (e->angleDelta().y() > 0) ? step : 1.0 / step;
    double target = transform().m11() * factor;
    if (target < 1e-8 || target > 1e3) { e->accept(); return; }
    scale(factor, factor);
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
