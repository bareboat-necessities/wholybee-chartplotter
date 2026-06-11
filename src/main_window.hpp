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
class Simulator;
class NavDataBrowserWindow;
class AisOverlay;
class AisTargetInfoWindow;
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
    void publishOwnshipToView();
    void onCursorMoved(double lon, double lat);
    void onScanProgress(int done, int total);
    void onScanFinished(bool ok, const QString& message);
    void onViewStatus(const QString& text);

private:
    void startScan(const QString& dir);
    void positionMenuButton();

    ChartView*    view_ = nullptr;
    ChartCatalog* catalog_ = nullptr;
    Settings*     settings_ = nullptr;
    SideMenu*     sideMenu_ = nullptr;
    NavDataStore* navStore_ = nullptr;
    AisTargetStore* aisStore_ = nullptr;
    Simulator*    simulator_ = nullptr;
    NavDataBrowserWindow* navBrowser_ = nullptr;
    std::unique_ptr<AisOverlay>    aisOverlay_;  // core-owned AIS chart overlay
    // Open AIS info windows, keyed by MMSI; QPointer auto-clears on close
    // (windows are WA_DeleteOnClose), so clicking a target again creates a
    // fresh window rather than raising a destroyed one.
    QHash<quint32, QPointer<AisTargetInfoWindow>> aisInfoWindows_;
    DataSourceRegistry             registry_;    // nav sources (built-in + plugin)
    std::unique_ptr<CoreApi>       coreApi_;     // plugin-facing core services
    std::unique_ptr<PluginManager> plugins_;     // owns built-in plugins
    QPushButton*  menuButton_ = nullptr;
    QLabel*       statusLeft_ = nullptr;   // root folder + scan summary
    QLabel*       statusMid_ = nullptr;    // band / cells shown
    QLabel*       statusRight_ = nullptr;  // cursor lat/lon
    QString       root_;
};
