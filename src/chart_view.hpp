#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <memory>
#include <vector>
#include "chart_loader.hpp"

class QGraphicsItem;

// A QGraphicsView-based chart canvas. The scene holds one QGraphicsItem per
// ENC feature in projected (Mercator) coordinates; the view supplies the
// pan/zoom transform, spatial indexing, and culling. Scene Y is negated so
// that north points up (Qt scene Y grows downward).
class ChartView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ChartView(QWidget* parent = nullptr);

    void setChart(std::shared_ptr<ChartSet> chart);
    void fitToChart();

signals:
    void cursorMoved(double lon, double lat);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;

private:
    void rebuildScene();
    void updatePointLOD();

    QGraphicsScene scene_;
    std::shared_ptr<ChartSet> chart_;

    // Soundings + point symbols, hidden when zoomed out to avoid clutter.
    std::vector<QGraphicsItem*> pointItems_;
    bool pointsVisible_ = true;

    bool haveChart_ = false;
    bool userInteracted_ = false; // auto-fit on resize until the user takes over
};
