#pragma once
#include <QString>
#include <QPointF>
#include <QSize>
#include <QTransform>
#include <functional>
#include <cmath>
#include "projection.hpp"

class QPainter;
class QWidget;
class INavDataPublisher;
class NavDataStore;

// Plugin API surface (Milestone 3 in ProjectSpec.md).
//
// Built-in plugins use exactly the interfaces a dynamic (DLL/SO) plugin will use
// later, so the contract can be refined before ABI/loader concerns appear.
// Plugins never touch core internals directly: they publish through stable APIs,
// subscribe to core signals, contribute menu items, and draw via overlays — all
// through ICoreApi, which the core implements.

// ---- coordinate / viewport bridge -----------------------------------------

// A snapshot of the chart camera handed to overlays so they can place drawing in
// geographic terms without knowing how the canvas is implemented. Value type:
// valid only for the duration of one paint call.
struct ChartViewport {
    QTransform sceneToScreen;   // projected scene metres (Y down) -> device px
    double     ppm = 0.0;       // pixels per scene metre (zoom)
    QSize      size;            // viewport size in device px
    double     worldWidthM = 0.0;
    double     centerSceneX = 0.0;

    QSize  viewportSize()   const { return size; }
    double pixelsPerMetre() const { return ppm; }

    // Geographic <-> screen. geoToScreen picks the nearest world copy across the
    // 180 degree seam so points near the date line land on-screen.
    QPointF geoToScreen(double latDeg, double lonDeg) const {
        double sx = proj::lonToX(lonDeg);
        const double sy = -proj::latToY(latDeg);
        if (worldWidthM > 0.0)
            sx += std::round((centerSceneX - sx) / worldWidthM) * worldWidthM;
        return sceneToScreen.map(QPointF(sx, sy));
    }
    void screenToGeo(const QPointF& px, double& latDeg, double& lonDeg) const {
        const QPointF s = sceneToScreen.inverted().map(px);
        lonDeg = proj::xToLon(s.x());
        latDeg = proj::yToLat(-s.y());
    }
};

// Handle returned when a plugin registers as a navigation data source. The
// plugin uses it to drive the status dot next to its auto-created menu item.
// Owned by the core; valid until shutdown.
class IDataSource {
public:
    virtual ~IDataSource() = default;
    virtual void setActive(bool on) = 0;   // green dot on/off (plugin-controlled)
};

// A chart overlay: the controlled way for a plugin to draw on the canvas without
// owning scene items, z-order, or threading. The core calls paint() each frame
// after its own drawing, in device coordinates.
class IChartOverlay {
public:
    virtual ~IChartOverlay() = default;
    virtual void paint(QPainter& painter, const ChartViewport& viewport) = 0;
    // Optional pick support; default declines. (screen pixel hit.)
    virtual bool hitTest(const QPointF& /*screenPt*/) { return false; }
};

// ---- core services handed to plugins ---------------------------------------

// Stable services a plugin receives in initialize(). The core owns the nav
// store, chart canvas, menu, settings, and lifetimes; plugins act through here.
class ICoreApi {
public:
    virtual ~ICoreApi() = default;

    // Navigation data --------------------------------------------------------
    // Publish updates (per-value source/timestamp arbitration applies).
    virtual INavDataPublisher* navPublisher() = 0;
    // Read current state and connect to its ownshipChanged() signal to subscribe.
    virtual const NavDataStore* navData() const = 0;

    // Menu contributions -----------------------------------------------------
    // Append items to the main menu's Plugins section.
    virtual void addMenuAction(const QString& title,
                               std::function<void()> onTriggered) = 0;
    virtual void addMenuToggle(const QString& title, bool checked,
                               std::function<void(bool)> onToggled) = 0;

    // Data sources -----------------------------------------------------------
    // Register the plugin as a navigation data source. The core adds an item
    // named `name` under Settings > Data Connections; clicking it invokes
    // `onOpenSettings` (the plugin shows its own settings dialog). The returned
    // handle lets the plugin drive its status dot. Core owns the handle.
    virtual IDataSource* registerDataSource(const QString& name,
                                            std::function<void()> onOpenSettings) = 0;

    // Chart overlays ---------------------------------------------------------
    virtual void addChartOverlay(IChartOverlay* overlay) = 0;
    virtual void removeChartOverlay(IChartOverlay* overlay) = 0;
    virtual void requestChartRepaint() = 0;

    // A parent for plugin-created dialogs/windows.
    virtual QWidget* dialogParent() = 0;
};

// ---- the plugin itself ------------------------------------------------------

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    // Register contributions (menu items, overlays) and grab core handles.
    virtual void initialize(ICoreApi* core) = 0;
    // Release anything registered with the core.
    virtual void shutdown() = 0;
};
