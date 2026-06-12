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
#include "units.hpp"            // DepthUnit
#include "heading_source.hpp"   // HeadingSource
#include "plugin_api.hpp"       // IChartOverlay, ChartViewport
#include "sym_atlas.hpp"        // SymAtlas

class ChartCatalog;
class QTimer;
class QPushButton;

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
    Qt::PenStyle penStyle = Qt::SolidLine;   // SolidLine / DashLine / DotLine
    bool   isDepthContour = false;
};

// A whole cell, clipped to a region and ready to draw. drawOffsetX shifts it by a
// whole-world width so cells near the date line can be drawn on the far side of
// the 180° seam (longitude wrap-around).
// A single sounding: scene position plus the raw depth in metres (S-57's native
// unit). The label is formatted at paint time from the current depth unit, so
// switching feet/metres is a repaint — no rebuild of the cell geometry.
struct Sounding {
    QPointF pos;            // scene position (Y already flipped north-up)
    double  depthM = 0.0;   // raw depth, metres
    bool    hasDepth = false;
};

// A resolved symbol: scene position + atlas index + optional rotation.
// symIdx == SymAtlas::kNoSymbol means no atlas entry was found; the renderer
// falls back to the magenta dot used before symbol support was added.
// rotationDeg is the S-57 ORIENT angle (degrees CW from true north); zero for
// upright symbols.  Resolved at cell-build time so paint is just a blit.
struct BuiltSymbol {
    QPointF  pos;
    uint16_t symIdx = SymAtlas::kNoSymbol;
    float    rotationDeg = 0.0f;
};

struct BuiltCell {
    QString path;
    int  band = 0;
    BBox clipBox;                                          // region (real frame)
    double drawOffsetX = 0.0;                              // scene-X wrap offset
    std::vector<BuiltPath>   paths;                        // sorted by z
    std::vector<Sounding>    soundings;                    // scene pos + depth
    std::vector<BuiltSymbol> symbols;                      // scene pos + sym idx
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
    // Recenter on the ownship without changing zoom. No-op if no fix is shown.
    void centerOnOwnship();
    // Auto-follow: keep the view centered on the ownship as it moves. Panning
    // turns it off (zooming does not). No-op-until-data if no fix yet.
    void setAutoFollow(bool on);
    bool autoFollow() const { return autoFollow_; }

    // Chart display settings (driven by the core Settings object).
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);
    // When true, soundings/symbols are skipped during a pan/zoom gesture (faster
    // moving frame); when false they stay visible while interacting.
    void setHideSymbolsWhilePanning(bool on);
    // Detail-level bias, in fractional bands. 0 = nominal mapping from visible
    // width to band; positive pulls in higher-detail cells (more detail on
    // screen); negative backs off. Range -2.0..+2.0.
    void setChartDetailLevel(double level);
    // Symbol scale factor. 1.0 = nominal (baked) size; range 0.5 .. 3.0.
    void setSymbolScale(double scale);
    // Vessel glyph scale factor (ownship + AIS). 1.0 = nominal; range 0.5..3.0.
    void setVesselScale(double scale);
    void setDepthUnit(DepthUnit u);   // relabels soundings (repaint, no rebuild)
    void setDistanceUnit(DistanceUnit u);   // scale-bar units (repaint)

    // Folder holding GSHHG data (containing GSHHS_shp/). Empty triggers a search
    // of the standard install locations. Loads the basemap underlay async.
    void setBasemapDirectory(const QString& dir);

    // Ownship overlay: the view subscribes to a NavDataStore and draws the
    // ownship symbol over the chart, with appearance reflecting freshness.
    void setOwnship(const OwnshipState& s);   // freshness read per-value from s
    // Length of the course-prediction line, in minutes of run-time at SOG.
    void setOwnshipPredictionMinutes(double minutes);
    // Which direction the ownship glyph points (heading vs COG).
    void setHeadingSource(HeadingSource s);

    // Plugin chart overlays: drawn on top of the chart each frame, in registration
    // order. The view does not own them (the plugin does).
    void addOverlay(IChartOverlay* overlay);
    void removeOverlay(IChartOverlay* overlay);

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
    // Auto-follow turned on/off (e.g. off when the user pans). Lets the menu
    // keep its checkmark in sync.
    void autoFollowChanged(bool on);
    // The user interacted with the chart itself — an empty-space click, a pan,
    // or a zoom. Transient popups (e.g. the AIS quick-info window) listen for
    // this to dismiss themselves. Not emitted when a click hits a chart overlay
    // (e.g. an AIS target), so target clicks keep their own handling.
    void chartInteracted();

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
    bool   recenterOnOwnship();   // move center to the ownship; false if no fix

    bool soundingVisible() const { return showSoundings_ && pointLodVisible_; }
    bool symbolVisible()   const { return showSymbols_   && pointLodVisible_; }
    bool contourVisible()  const { return showDepthContours_; }

    // Format a sounding label from raw metres using the current depth unit.
    QString formatSounding(double depthM) const;

    // Minimum on-screen spacing (device px) between drawn soundings, given the
    // label line height. Grows with the detail level so the denser soundings
    // pulled in at higher detail don't pile up; returns 0 (no thinning) at or
    // below nominal detail, keeping level 0 identical to before.
    double soundingMinSpacing(double lineHeightPx) const;

    void drawOwnship(QPainter& p, const QTransform& cam);
    void drawScaleBar(QPainter& p);   // lower-right scale bar, in device pixels
    // Touch-friendly zoom: same step as the wheel, anchored at the screen
    // centre (no cursor on touch devices).
    void zoomBy(double factor);
    void positionZoomButtons();       // place + / - buttons left of the scale bar

    // Camera state (scene metres = projected Mercator, Y flipped north-up).
    double scx_ = 0.0;
    double scy_ = 0.0;
    double ppm_ = 0.0;     // pixels per metre; 0 until a catalog/view is set

    ChartCatalog* catalog_ = nullptr;
    QThreadPool   pool_;
    QTimer*       updateTimer_ = nullptr;
    QTimer*       aaTimer_ = nullptr;
    QTimer*       saveTimer_ = nullptr;
    QPushButton*  zoomInBtn_ = nullptr;     // lower-right + button
    QPushButton*  zoomOutBtn_ = nullptr;    // lower-right - button

    QHash<QString, BuiltCell> loaded_;
    QHash<QString, int>       bandByPath_;
    QHash<QString, BBox>      bboxByPath_;
    // Quilting (computed in updateVisibleCells, consumed in paintEvent):
    //   active_   — loaded cells that contribute pixels (finest band in their
    //               region); cells fully covered by finer bands are excluded.
    //   drawClip_ — for a cell only partially covered by finer bands, the scene-
    //               frame (cell-native, no wrap offset) path it may draw within.
    //               Absent ⇒ draw unclipped. Keyed by cell path.
    QSet<QString>             active_;
    QHash<QString, QPainterPath> drawClip_;
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

    // Symbol atlas (prebaked from chartsymbols.xml + rastersymbols-day.png).
    // Loaded once at construction; immutable after that, safe to query from
    // worker threads.  When not loaded, point features fall back to a dot.
    SymAtlas symAtlas_;

    bool   havePendingView_ = false;
    double pendingLon_ = 0.0, pendingLat_ = 0.0, pendingScale_ = 0.0;

    bool haveCatalog_ = false;
    bool interacting_ = false;        // drop antialiasing mid-gesture
    bool pointLodVisible_ = true;     // soundings/symbols shown at this zoom (LOD)
    bool showSoundings_ = true;
    bool showSymbols_ = true;
    bool showDepthContours_ = true;
    bool hideSymbolsWhilePanning_ = false;   // skip point overlays during a gesture
    double chartDetailLevel_ = 0.0;   // -2.0..+2.0, biases target band
    double symbolScale_      = 1.0;   // 0.5..3.0, uniform symbol scale
    double vesselScale_      = 1.0;   // 0.5..3.0, ownship + AIS glyph scale
    DepthUnit depthUnit_ = DepthUnit::Feet;   // how soundings are labelled
    DistanceUnit distanceUnit_ = DistanceUnit::NauticalMiles;   // scale-bar units
    bool userInteracted_ = false;
    bool autoFollow_ = false;                 // keep view centered on ownship
    std::vector<IChartOverlay*> overlays_;    // plugin overlays (not owned)

    bool    dragging_ = false;
    bool    panDismissEmitted_ = false;   // chartInteracted() fired once per drag
    QPointF lastDragPos_;
    QPointF pressPos_;     // for click vs drag (release with little movement = click)

    OwnshipState ownship_;
    NavFreshness ownshipFreshness_ = NavFreshness::Invalid;
    double       ownshipPredMin_ = 6.0;   // predictor length (minutes)
    HeadingSource headingSource_ = HeadingSource::Heading;   // ownship glyph direction
};
