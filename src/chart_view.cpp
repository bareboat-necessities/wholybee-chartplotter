#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "projection.hpp"

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

QPainterPath buildPath(const Feature& f, bool closed) {
    QPainterPath path;
    if (closed) path.setFillRule(Qt::OddEvenFill);
    for (const auto& ring : f.rings) {
        if (ring.size() < 2) continue;
        path.moveTo(ring[0].x, -ring[0].y);
        for (std::size_t i = 1; i < ring.size(); ++i)
            path.lineTo(ring[i].x, -ring[i].y);
        if (closed) path.closeSubpath();
    }
    return path;
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
    pointsVisible_ = true;
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
    for (const CellRecord& c : catalog_->cells())
        bandByPath_.insert(c.path, c.band);
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

void ChartView::updateVisibleCells() {
    if (!haveCatalog_ || !catalog_) return;

    // View rectangle in projected (north-up) world coordinates.
    const QRectF sr = mapToScene(viewport()->rect()).boundingRect();
    BBox view;
    view.minx = sr.left();  view.maxx = sr.right();
    view.miny = -sr.bottom(); view.maxy = -sr.top();   // scene Y is flipped

    const double pxPerMeter = transform().m11();
    if (pxPerMeter <= 0.0) return;
    const double visWidthM = viewport()->width() / pxPerMeter;
    const int target = bandForVisibleWidth(visWidthM);

    const BBox wantedArea = expandBox(view, 0.5);   // load a little beyond the edge
    const BBox keepArea   = expandBox(view, 1.5);   // unload only well beyond it

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

    // Dispatch loads for newly-wanted cells.
    for (const QString& path : wanted_)
        if (!loaded_.contains(path) && !inFlight_.contains(path))
            dispatchLoad(path);

    // Unload cells outside keepArea or whose band is now finer than maxBand.
    const QList<QString> loadedPaths = loaded_.keys();
    for (const QString& path : loadedPaths) {
        const CellRecord* rec = nullptr;
        for (const CellRecord& c : cells) { if (c.path == path) { rec = &c; break; } }
        const bool bandOk = rec && (rec->band == 0 || (rec->band >= 1 && rec->band <= maxBand));
        const bool keep = rec && rec->extentValid && bandOk && rec->bbox.intersects(keepArea);
        if (!keep) removeCell(path);
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
        const CellLoadResult r = watcher->result();
        watcher->deleteLater();
        onCellLoaded(r, gen);
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

void ChartView::onCellLoaded(const CellLoadResult& r, quint64 gen) {
    if (gen != generation_) return;            // result from a superseded scan
    inFlight_.remove(r.path);
    if (!r.ok) return;
    if (!wanted_.contains(r.path)) return;     // view moved on; drop it
    if (loaded_.contains(r.path)) return;      // already present
    addCell(r.path, r.features, bandByPath_.value(r.path, 0));
    emit statusChanged(QStringLiteral("%1 cell(s) shown").arg(loaded_.size()));
}

void ChartView::addCell(const QString& path, const std::vector<Feature>& feats, int band) {
    LoadedCell cell;
    cell.band = band;
    // Band-major depth: coarser bands sit beneath finer ones, so a finer cell's
    // opaque area fills cover the coarse data within its footprint, while gaps
    // reveal the coarser band underneath. Feature z-order orders within a band.
    const double zb = static_cast<double>(band) * 1000.0;
    for (const Feature& f : feats) {
        switch (f.kind) {
            case FeatureKind::DepthArea:
            case FeatureKind::LandArea: {
                auto* item = new QGraphicsPathItem(buildPath(f, true));
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
                auto* item = new QGraphicsPathItem(buildPath(f, false));
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
                break;
            }
            case FeatureKind::Sounding: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                QString text = f.hasDepth
                    ? QString::number(f.depth, 'f', f.depth < 10.0 ? 1 : 0)
                    : QStringLiteral(".");
                auto* t = new QGraphicsSimpleTextItem(text);
                QFont fnt = t->font(); fnt.setPointSizeF(8.0); t->setFont(fnt);
                t->setBrush(QColor(26, 51, 115));
                t->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                t->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                t->setZValue(zb + f.zorder);
                t->setVisible(pointsVisible_);
                scene_.addItem(t);
                cell.items.push_back(t);
                cell.points.push_back(t);
                break;
            }
            case FeatureKind::Point: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                auto* e = new QGraphicsEllipseItem(-3.0, -3.0, 6.0, 6.0);
                e->setBrush(QColor(179, 26, 128));
                e->setPen(Qt::NoPen);
                e->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                e->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                e->setZValue(zb + f.zorder);
                e->setVisible(pointsVisible_);
                scene_.addItem(e);
                cell.items.push_back(e);
                cell.points.push_back(e);
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
    if (show == pointsVisible_) return;
    pointsVisible_ = show;
    for (auto it = loaded_.begin(); it != loaded_.end(); ++it)
        for (QGraphicsItem* p : it->points)
            p->setVisible(show);
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
                QStringLiteral("Open a chart folder to begin (toolbar button)."));
    p->restore();
}
