#pragma once
#include <QWidget>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QPainterPath>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QThreadPool>
#include <vector>
#include <utility>
#include <memory>
#include "chart_loader.hpp"
#include "feature_cache.hpp"
#include "nav_data_store.hpp"   // OwnshipState, NavFreshness

class ChartCatalog;
class QTimer;

// Result handed back from a worker thread after loading one cell.
struct CellLoadResult {
    QString path;
    std::vector<Feature> features;
    BBox bbox;
    bool ok = false;
    QString error;
};

// One ready-to-draw vector primitive: a clipped, simplified path plus its style.
// Built on a worker thread (QPainterPath/QColor are value types, safe off the
// GUI thread); the UI thread draws it directly through the camera transform.
// Coordinates are scene metres: projected Mercator with Y flipped so north is up.
struct BuiltPath {
    QPainterPath path;
    QRectF bounds;             // path.boundingRect(), for view culling
    double z = 0.0;
    bool   filled = false;     // true: area (use brush); false: line (pen only)
    QColor brush;
    bool   hasPen = false;
    QColor penColor;
    qreal  penWidth = 1.0;
    bool   isDepthContour = false;
};

// A whole cell, clipped to a region and ready to draw. drawOffsetX shifts it by a
// whole-world width so cells near the date line can be drawn on the far side of
// the 180° seam (longitude wrap-around).
struct BuiltCell {
    QString path;
    int  band = 0;
    BBox clipBox;                                          // region (real frame)
    double drawOffsetX = 0.0;                              // scene-X wrap offset
    std::vector<BuiltPath> paths;                          // sorted by z
    std::vector<std::pair<QPointF, QString>> soundings;    // scene pos + label
    std::vector<QPointF> symbols;                          // scene pos
};

// Chart canvas with a camera-based renderer.
//
// The view is a camera: a center point in projected Mercator metres (scene
// coordinates) plus a zoom in pixels-per-metre. Geometry is painted directly
// with QPainter through the camera transform, rather than via QGraphicsScene.
// This makes panning truly unbounded and — because Mercator longitude is linear
// in X with period worldWidth — lets us wrap at the 180° seam by drawing cells
// shifted by whole-world widths. It also leaves a clean place to draw a
// worldwide basemap underlay beneath the cells.
class ChartView : public QWidget {
    Q_OBJECT
public:
    explicit ChartView(QWidget* parent = nullptr);

    void setCatalog(ChartCatalog* catalog);
    void fitToCatalog();

    // Chart display settings (driven by the core Settings object).
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);

    // Folder holding GSHHG data (containing GSHHS_shp/). Empty triggers a search
    // of the standard install locations. Loads the basemap underlay async.
    void setBasemapDirectory(const QString& dir);

    // Ownship overlay: the view subscribes to a NavDataStore and draws the
    // ownship symbol over the chart, with appearance reflecting freshness.
    void setOwnship(const OwnshipState& s, NavFreshness f);
    // Length of the course-prediction line, in minutes of run-time at SOG.
    void setOwnshipPredictionMinutes(double minutes);

    // Restore the view (center in degrees + zoom) on the next catalog load
    // instead of fitting. One-shot: consumed on the next load.
    void setInitialView(double lon, double lat, double scale);
    // Emit viewChanged immediately with the current view (e.g. on app close).
    void persistViewNow();

signals:
    void cursorMoved(double lon, double lat);
    void statusChanged(const QString& text);
    // Debounced after panning/zooming; carries the view center (degrees) + zoom.
    void viewChanged(double lon, double lat, double scale);

protected:
    void paintEvent(QPaintEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onCatalogFinished(bool ok, const QString& message);
    void scheduleUpdate();
    void updateVisibleCells();

private:
    void dispatchLoad(const QString& path);
    void onCellLoaded(CellLoadResult r, quint64 gen);
    void dispatchBuild(const QString& path, FeatureCache::FeaturesPtr feats,
                       int band, const BBox& clipBox, double drawOffsetX);
    void onCellBuilt(BuiltCell bc, quint64 gen, double drawOffsetX);
    void storeCell(BuiltCell bc);
    void removeCell(const QString& path);
    void clearAll();
    void updatePointLOD();

    // Basemap (GSHHG land/lakes underlay) -----------------------------------
    void reloadBasemap();          // resolve directory + available tiers
    void ensureViewForBasemap();   // whole-world view when basemap shows w/o charts
    QString desiredTier() const;   // tier matching the current zoom + availability
    void ensureTierForZoom();      // switch to / load the desired tier
    void loadTier(const QString& tier);
    void onTierLoaded(FeatureCache::FeaturesPtr feats, const QString& tier);
    void maybeBuildBasemap();      // rebuild clipped/simplified copies if needed
    void onBasemapBuilt(std::vector<BuiltCell> cells, FeatureCache::FeaturesPtr feats);

    bool computeViewBoxes(BBox& view, BBox& wanted, BBox& keep, int& target) const;
    static int  bandForVisibleWidth(double metres);
    static BBox expandBox(const BBox& b, double frac);
    static BBox shiftX(const BBox& b, double dx);

    // Camera ----------------------------------------------------------------
    QTransform cameraTransform() const;     // scene metres -> screen pixels
    QPointF screenToScene(const QPointF& screen) const;
    static double worldWidthM();            // scene width of 360° of longitude
    double minPpm() const;                  // most zoomed-out: globe fills width once
    void   normalizeCenter();               // wrap center X into [-W, W)
    double wrapOffsetFor(double cellCenterX) const;  // nearest whole-world shift
    void   restoreView(double lon, double lat, double scale);
    bool   currentView(double& lon, double& lat, double& scale) const;
    void   beginInteraction();

    bool soundingVisible() const { return showSoundings_ && pointLodVisible_; }
    bool symbolVisible()   const { return showSymbols_   && pointLodVisible_; }
    bool contourVisible()  const { return showDepthContours_; }

    void drawOwnship(QPainter& p, const QTransform& cam);

    // Camera state (scene metres = projected Mercator, Y flipped north-up).
    double scx_ = 0.0;
    double scy_ = 0.0;
    double ppm_ = 0.0;     // pixels per metre; 0 until a catalog/view is set

    ChartCatalog* catalog_ = nullptr;
    QThreadPool   pool_;
    QTimer*       updateTimer_ = nullptr;
    QTimer*       aaTimer_ = nullptr;
    QTimer*       saveTimer_ = nullptr;

    QHash<QString, BuiltCell> loaded_;
    QHash<QString, int>       bandByPath_;
    QHash<QString, BBox>      bboxByPath_;
    FeatureCache              cache_;
    QSet<QString> inFlight_;     // parse running on a worker
    QSet<QString> building_;     // clip/build running on a worker
    QSet<QString> wanted_;       // last computed wanted set
    quint64       generation_ = 0;

    // Basemap state. basemap_ holds the clipped/simplified copies (one per wrap
    // offset) currently drawn beneath the cells. The active tier is chosen by
    // zoom; loaded tiers are cached so re-zooming is instant.
    QString                   basemapDir_;       // user override ("" = search)
    QString                   basemapRoot_;      // resolved root with GSHHS_shp/
    QStringList               availableTiers_;   // tiers present in basemapRoot_
    QString                   basemapTier_;      // active tier ("" = none)
    QString                   tierLoading_;      // tier whose load is in flight
    FeatureCache              tierCache_;        // loaded tiers (LRU, active pinned)
    FeatureCache::FeaturesPtr basemapFeats_;     // active tier's features
    std::vector<BuiltCell>    basemap_;
    BBox    basemapClipBox_;          // region basemap_ was built for (k=0 frame)
    double  basemapBuiltPpm_ = 0.0;   // zoom basemap_ was simplified for
    bool    basemapBuilding_ = false;

    bool   havePendingView_ = false;
    double pendingLon_ = 0.0, pendingLat_ = 0.0, pendingScale_ = 0.0;

    bool haveCatalog_ = false;
    bool interacting_ = false;        // drop antialiasing mid-gesture
    bool pointLodVisible_ = true;     // soundings/symbols shown at this zoom (LOD)
    bool showSoundings_ = true;
    bool showSymbols_ = true;
    bool showDepthContours_ = true;
    bool userInteracted_ = false;

    bool    dragging_ = false;
    QPointF lastDragPos_;

    OwnshipState ownship_;
    NavFreshness ownshipFreshness_ = NavFreshness::Invalid;
    double       ownshipPredMin_ = 6.0;   // predictor length (minutes)
};
