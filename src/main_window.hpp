#pragma once
#include <QMainWindow>
#include <QString>

class ChartView;
class ChartCatalog;
class Settings;
class SideMenu;
class NavDataStore;
class Simulator;
class Nmea0183Client;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

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
    void editNmea();
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
    Simulator*    simulator_ = nullptr;
    Nmea0183Client* nmea_ = nullptr;
    QPushButton*  menuButton_ = nullptr;
    QLabel*       statusLeft_ = nullptr;   // root folder + scan summary
    QLabel*       statusMid_ = nullptr;    // band / cells shown
    QLabel*       statusRight_ = nullptr;  // cursor lat/lon
    QString       root_;
};
