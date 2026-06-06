#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QThreadPool>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
#include <vector>
#include <memory>
#include "chart_loader.hpp"
#include "feature_cache.hpp"

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

    // Chart display settings (driven by the core Settings object). Each toggles
    // a category of already-loaded items and is honored as new cells load.
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);

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
        QVector<QGraphicsItem*> items;      // everything for this cell
        QVector<QGraphicsItem*> soundings;  // depth figures (LOD + user toggle)
        QVector<QGraphicsItem*> symbols;    // point objects (LOD + user toggle)
        QVector<QGraphicsItem*> contours;   // depth contour lines (user toggle)
        int  band = 0;
        BBox clipBox;                       // region this cell's geometry was clipped to
    };

    void dispatchLoad(const QString& path);
    void onCellLoaded(CellLoadResult r, quint64 gen);
    // Build (or rebuild) one cell's scene items from parsed features, clipping
    // geometry to clipBox. Replaces any existing items for that path.
    void addCell(const QString& path, const std::vector<Feature>& feats,
                 int band, const BBox& clipBox);
    void removeCell(const QString& path);
    void clearAll();
    void updatePointLOD();

    // Current viewport expressed as world boxes (projected, north-up) plus the
    // zoom-appropriate target band. Returns false if the transform isn't usable
    // yet. wanted = load/clip-trigger region, keep = clip + unload region.
    bool computeViewBoxes(BBox& view, BBox& wanted, BBox& keep, int& target) const;

    static int  bandForVisibleWidth(double metres);
    static BBox expandBox(const BBox& b, double frac);

    // Effective visibility for each toggled category: the user's preference
    // combined with the zoom-driven LOD where it applies.
    bool soundingVisible() const { return showSoundings_ && pointLodVisible_; }
    bool symbolVisible()   const { return showSymbols_   && pointLodVisible_; }
    bool contourVisible()  const { return showDepthContours_; }

    QGraphicsScene scene_;
    ChartCatalog*  catalog_ = nullptr;
    QThreadPool    pool_;
    QTimer*        updateTimer_ = nullptr;

    QHash<QString, LoadedCell> loaded_;
    QHash<QString, int>        bandByPath_;   // band for every cataloged cell
    QHash<QString, BBox>       bboxByPath_;   // footprint for every cataloged cell
    FeatureCache               cache_;        // LRU of parsed cells, keyed by path
    QSet<QString>  inFlight_;
    QSet<QString>  wanted_;       // last computed wanted set (for late arrivals)
    quint64        generation_ = 0;

    bool haveCatalog_ = false;
    bool pointLodVisible_ = true;     // soundings/symbols shown at this zoom (LOD)
    bool showSoundings_ = true;       // user preference (from Settings)
    bool showSymbols_ = true;
    bool showDepthContours_ = true;
    bool userInteracted_ = false;
};
