#pragma once
#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QString>
#include <memory>
#include "data_sources.hpp"   // DataSourceRegistry (value member)

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
class CoreApi;
class PluginManager;
class QLabel;
class QPushButton;

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
    DataSourceRegistry             registry_;    // nav sources (built-in + plugin)
    std::unique_ptr<CoreApi>       coreApi_;     // plugin-facing core services
    std::unique_ptr<PluginManager> plugins_;     // owns built-in plugins
    QPushButton*  menuButton_ = nullptr;
    QLabel*       statusLeft_ = nullptr;   // root folder + scan summary
    QLabel*       statusMid_ = nullptr;    // band / cells shown
    QLabel*       statusRight_ = nullptr;  // cursor lat/lon
    QString       root_;
    // Latest scan results for the active folder, reconciled into one status line
    // (ENC scan and raster discovery finish independently / out of order).
    bool          encScanDone_ = false;
    bool          encScanOk_ = false;
    QString       encScanMsg_;
    int           rasterCount_ = 0;
};
