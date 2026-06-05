#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QThreadPool>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
#include <vector>
#include "chart_loader.hpp"

class ChartCatalog;
class QTimer;
class QGraphicsItem;

// Result handed back from a worker thread after loading one cell.
struct CellLoadResult {
    QString path;
    std::vector<Feature> features;
    BBox bbox;
    bool ok = false;
    QString error;
};

// Canvas that shows only the ENC cells intersecting the current view, choosing a
// usage band by zoom level and loading cells asynchronously as the view moves.
class ChartView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ChartView(QWidget* parent = nullptr);

    // Attach the catalog; the view reacts to its finished() signal.
    void setCatalog(ChartCatalog* catalog);

    void fitToCatalog();

signals:
    void cursorMoved(double lon, double lat);
    void statusChanged(const QString& text);  // e.g. "Band 5 · 7 cells"

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;

private slots:
    void onCatalogFinished(bool ok, const QString& message);
    void scheduleUpdate();
    void updateVisibleCells();

private:
    struct LoadedCell {
        QVector<QGraphicsItem*> items;   // everything for this cell
        QVector<QGraphicsItem*> points;  // soundings + point symbols (LOD-toggled)
    };

    void dispatchLoad(const QString& path);
    void onCellLoaded(const CellLoadResult& r, quint64 gen);
    void addCell(const QString& path, const std::vector<Feature>& feats);
    void removeCell(const QString& path);
    void clearAll();
    void updatePointLOD();

    static int  bandForVisibleWidth(double metres);
    static BBox expandBox(const BBox& b, double frac);

    QGraphicsScene scene_;
    ChartCatalog*  catalog_ = nullptr;
    QThreadPool    pool_;
    QTimer*        updateTimer_ = nullptr;

    QHash<QString, LoadedCell> loaded_;
    QSet<QString>  inFlight_;
    QSet<QString>  wanted_;       // last computed wanted set (for late arrivals)
    quint64        generation_ = 0;

    bool haveCatalog_ = false;
    bool pointsVisible_ = true;
    bool userInteracted_ = false;
};
