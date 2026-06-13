#pragma once
#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QString>
#include <memory>
#include "data_sources.hpp"   // DataSourceRegistry (value member)
#include "route_types.hpp"    // Route (value member for the props working copy)

class ChartView;
class ChartCatalog;
class Settings;
class SideMenu;
class NavDataStore;
class AisTargetStore;
class CpaCalculator;
class Simulator;
class NavDataBrowserWindow;
class AisOverlay;
class AisTargetInfoWindow;
class AisQuickInfoWindow;
class AisTargetListDialog;
class RouteStore;
class RouteOverlay;
class RouteListDialog;
class WaypointListDialog;
class RoutePropertiesDialog;
class CoreApi;
class PluginManager;
class QLabel;
class QPushButton;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;   // out-of-line: unique_ptr to incomplete plugin types

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onChartSetSelected(const QString& dir);
    void manageChartSets();
    void chooseBasemapFolder();
    void editUnits();
    void editStaleThresholds();
    void editOwnshipPrediction();
    void showNavDataBrowser();
    void editDataPriority();
    void editChartDetailLevel();
    void editSymbolSize();
    void editVesselSize();
    void editOwnshipMmsi();
    void editHeadingSource();
    void editDangerousShips();
    void showAisTargetList();
    // Routes & Waypoints.
    void startCreateRoute();
    void startEditRoute();
    void startCreateWaypoint();
    void startEditWaypoint();
    void dropWaypoint();
    void showRouteList();
    void showWaypointList();
    void openRouteProperties(qint64 id);   // from List Routes "Properties"
    void publishOwnshipToView();
    void onCursorMoved(double lon, double lat);
    void onScanProgress(int done, int total);
    void onScanFinished(bool ok, const QString& message);
    void onRasterChartsChanged(int count);
    void onViewStatus(const QString& text);

private:
    void startScan(const QString& dir);
    void refreshChartStatus();   // compose status from ENC + raster results
    void positionMenuButton();

    // Routes & Waypoints helpers ---------------------------------------------
    void buildEditBar();              // construct the floating action bar (hidden)
    void positionEditBar();           // centre it over the chart top
    void showRouteEditBar(const QString& hint);    // Complete/Delete/Cancel
    void showWaypointPlaceBar(const QString& hint); // Cancel only
    void showWaypointEditBar(const QString& hint);  // Done/Cancel (no Delete Point)
    void showPointDragBar(const QString& hint);     // Done/Cancel for props-drag
    void endRouteMode();              // clear overlay edit + editor + bar
    void beginEditRoute(qint64 id);   // fit chart + start editing a saved route
    void beginEditWaypoint(qint64 id);// fit chart + start moving a saved waypoint
    void completeEdit();              // Complete/Done button: dispatch by mode
    void cancelEdit();                // Cancel button: dispatch by mode
    void completeRoute();             // name + persist the working route
    void completeWaypointMove();      // persist the moved waypoint
    void onWaypointPlaced(double lat, double lon);  // create-waypoint tap
    // Route Properties drag round-trip.
    void onPropsEditPoint(int index); // dialog asked to drag point `index`
    void finishPropsDrag(bool apply); // return from chart drag to the dialog

    ChartView*    view_ = nullptr;
    ChartCatalog* catalog_ = nullptr;
    Settings*     settings_ = nullptr;
    SideMenu*     sideMenu_ = nullptr;
    NavDataStore* navStore_ = nullptr;
    AisTargetStore* aisStore_ = nullptr;
    CpaCalculator* cpaCalc_ = nullptr;   // keeps target CPA/TCPA up to date
    Simulator*    simulator_ = nullptr;
    NavDataBrowserWindow* navBrowser_ = nullptr;
    std::unique_ptr<AisOverlay>    aisOverlay_;  // core-owned AIS chart overlay
    // Open AIS info windows, keyed by MMSI; QPointer auto-clears on close
    // (windows are WA_DeleteOnClose), so clicking a target again creates a
    // fresh window rather than raising a destroyed one.
    QHash<quint32, QPointer<AisTargetInfoWindow>> aisInfoWindows_;
    // Quick-look popup: the first click on a target shows this; a second click
    // on the same target opens the full window. Only one exists at a time; any
    // chart interaction dismisses it. QPointer clears when it self-deletes.
    QPointer<AisQuickInfoWindow> aisQuickInfo_;
    quint32 aisQuickInfoMmsi_ = 0;
    // Reused list window: one instance per session, raised on subsequent opens.
    QPointer<AisTargetListDialog> aisListDlg_;
    void showAisTarget(quint32 mmsi);   // drives the two-click open behaviour

    // Routes & Waypoints -----------------------------------------------------
    RouteStore* routeStore_ = nullptr;
    std::unique_ptr<RouteOverlay> routeOverlay_;
    QPointer<RouteListDialog>     routeListDlg_;
    QPointer<WaypointListDialog>  waypointListDlg_;
    // Route Properties editor + the drag round-trip it hands off to the chart.
    QPointer<RoutePropertiesDialog> propsDlg_;
    Route propsWork_;                  // working route while Properties is open
    enum class EditContext { None, RouteProps };
    EditContext editContext_ = EditContext::None;
    // Floating action bar shown over the chart during a create/edit session.
    QWidget*     editBar_ = nullptr;
    QLabel*      editHint_ = nullptr;
    QPushButton* completeBtn_ = nullptr;
    QPushButton* deletePointBtn_ = nullptr;
    QPushButton* cancelEditBtn_ = nullptr;
    DataSourceRegistry             registry_;    // nav sources (built-in + plugin)
    std::unique_ptr<CoreApi>       coreApi_;     // plugin-facing core services
    std::unique_ptr<PluginManager> plugins_;     // owns built-in plugins
    QPushButton*  menuButton_ = nullptr;
    QLabel*       statusLeft_ = nullptr;   // root folder + scan summary
    QLabel*       statusMid_ = nullptr;    // band / cells shown
    QLabel*       statusRight_ = nullptr;  // cursor lat/lon
    QString       root_;
    double        lastCursorLon_ = 0.0;   // last cursor pos, for re-rendering the
    double        lastCursorLat_ = 0.0;   // status bar when the coord format changes
    // Latest scan results for the active folder, reconciled into one status line
    // (ENC scan and raster discovery finish independently / out of order).
    bool          encScanDone_ = false;
    bool          encScanOk_ = false;
    QString       encScanMsg_;
    int           rasterCount_ = 0;
};
