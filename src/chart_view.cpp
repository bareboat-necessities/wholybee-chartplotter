#include "chart_view.hpp"
#include "projection.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsEllipseItem>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <cmath>

namespace {

// Mirrors the depth/land colour ramp from the GTK build (shallow = darker
// blue, deep = near white).
QColor fillColor(const Feature& f) {
    if (f.kind == FeatureKind::LandArea) return QColor(217, 199, 148);
    if (!f.hasDepth)                     return QColor(184, 212, 235);
    double d = f.depth;
    if (d <  0.0)  return QColor(158, 189, 140); // drying
    if (d <  2.0)  return QColor(102, 168, 217);
    if (d <  5.0)  return QColor(143, 194, 227);
    if (d < 10.0)  return QColor(184, 217, 240);
    if (d < 20.0)  return QColor(217, 235, 250);
    return QColor(242, 247, 255);
}

// Build a QPainterPath from a feature's rings, flipping Y for north-up.
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
    setDragMode(QGraphicsView::ScrollHandDrag);          // drag to pan
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse); // zoom under cursor
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    viewport()->setMouseTracking(true);
}

void ChartView::setChart(std::shared_ptr<ChartSet> chart) {
    chart_ = std::move(chart);
    rebuildScene();
    fitToChart();
}

void ChartView::rebuildScene() {
    scene_.clear();          // deletes all items
    pointItems_.clear();
    pointsVisible_ = true;
    userInteracted_ = false;
    haveChart_ = (chart_ && chart_->bounds().valid());
    if (!haveChart_) {
        scene_.setSceneRect(QRectF());
        viewport()->update();
        return;
    }

    const BBox& b = chart_->bounds();
    // Flipped-Y scene rect: top edge is -maxy.
    scene_.setSceneRect(b.minx, -b.maxy, b.maxx - b.minx, b.maxy - b.miny);

    for (const Feature& f : chart_->features()) {
        switch (f.kind) {
            case FeatureKind::DepthArea:
            case FeatureKind::LandArea: {
                auto* item = new QGraphicsPathItem(buildPath(f, true));
                item->setBrush(fillColor(f));
                if (f.kind == FeatureKind::LandArea) {
                    QPen pen(QColor(115, 97, 64));
                    pen.setCosmetic(true);
                    pen.setWidthF(1.0);
                    item->setPen(pen);
                } else {
                    item->setPen(Qt::NoPen);
                }
                item->setZValue(f.zorder);
                scene_.addItem(item);
                break;
            }
            case FeatureKind::OtherArea:
            case FeatureKind::DepthContour:
            case FeatureKind::Coastline:
            case FeatureKind::OtherLine: {
                auto* item = new QGraphicsPathItem(buildPath(f, false));
                item->setBrush(Qt::NoBrush);
                QPen pen;
                pen.setCosmetic(true); // constant pixel width regardless of zoom
                if (f.kind == FeatureKind::Coastline) {
                    pen.setColor(QColor(64, 51, 31));  pen.setWidthF(1.4);
                } else if (f.kind == FeatureKind::DepthContour) {
                    pen.setColor(QColor(115, 153, 199)); pen.setWidthF(0.8);
                } else if (f.kind == FeatureKind::OtherArea) {
                    pen.setColor(QColor(102, 102, 115, 150)); pen.setWidthF(0.7);
                } else {
                    pen.setColor(QColor(102, 102, 128)); pen.setWidthF(0.8);
                }
                item->setPen(pen);
                item->setZValue(f.zorder);
                scene_.addItem(item);
                break;
            }
            case FeatureKind::Sounding: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                QString text;
                if (f.hasDepth)
                    text = QString::number(f.depth, 'f', f.depth < 10.0 ? 1 : 0);
                else
                    text = QStringLiteral(".");
                auto* t = new QGraphicsSimpleTextItem(text);
                QFont fnt = t->font();
                fnt.setPointSizeF(8.0);
                t->setFont(fnt);
                t->setBrush(QColor(26, 51, 115));
                t->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                t->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                t->setZValue(f.zorder);
                scene_.addItem(t);
                pointItems_.push_back(t);
                break;
            }
            case FeatureKind::Point: {
                if (f.rings.empty() || f.rings[0].empty()) break;
                auto* e = new QGraphicsEllipseItem(-3.0, -3.0, 6.0, 6.0);
                e->setBrush(QColor(179, 26, 128));
                e->setPen(Qt::NoPen);
                e->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
                e->setPos(f.rings[0][0].x, -f.rings[0][0].y);
                e->setZValue(f.zorder);
                scene_.addItem(e);
                pointItems_.push_back(e);
                break;
            }
        }
    }
}

void ChartView::fitToChart() {
    if (!haveChart_) return;
    QRectF r = scene_.sceneRect();
    double mx = r.width() * 0.04, my = r.height() * 0.04;
    fitInView(r.adjusted(-mx, -my, mx, my), Qt::KeepAspectRatio);
    updatePointLOD();
}

void ChartView::updatePointLOD() {
    if (!haveChart_) return;
    double pxPerMeter = transform().m11();
    if (pxPerMeter <= 0.0) return;
    double visWidthMeters = viewport()->width() / pxPerMeter;
    bool show = visWidthMeters < 20000.0; // ~20 km across
    if (show != pointsVisible_) {
        pointsVisible_ = show;
        for (QGraphicsItem* it : pointItems_)
            it->setVisible(show);
    }
}

void ChartView::wheelEvent(QWheelEvent* e) {
    if (!haveChart_) { QGraphicsView::wheelEvent(e); return; }
    userInteracted_ = true;
    const double step = 1.15;
    double factor = (e->angleDelta().y() > 0) ? step : 1.0 / step;
    double target = transform().m11() * factor;
    if (target < 1e-8 || target > 1e3) { e->accept(); return; } // clamp
    scale(factor, factor); // AnchorUnderMouse keeps the cursor point fixed
    updatePointLOD();
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
    userInteracted_ = true;
    QGraphicsView::mousePressEvent(e);
}

void ChartView::mouseMoveEvent(QMouseEvent* e) {
    if (haveChart_) {
        QPointF s = mapToScene(e->position().toPoint());
        emit cursorMoved(proj::xToLon(s.x()), proj::yToLat(-s.y()));
    }
    QGraphicsView::mouseMoveEvent(e); // keep ScrollHandDrag panning alive
}

void ChartView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (haveChart_ && !userInteracted_) fitToChart();
}

void ChartView::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, QColor(204, 224, 242)); // sea backdrop
}

void ChartView::drawForeground(QPainter* p, const QRectF&) {
    if (haveChart_) return;
    p->save();
    p->resetTransform(); // draw in device coordinates
    p->setPen(QColor(80, 80, 80));
    QFont f = p->font();
    f.setPointSize(13);
    p->setFont(f);
    p->drawText(viewport()->rect(), Qt::AlignCenter,
                QStringLiteral("Open a chart folder to begin (toolbar button)."));
    p->restore();
}
