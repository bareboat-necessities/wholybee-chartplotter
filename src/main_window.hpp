#pragma once
#include <QMainWindow>
#include <QString>

class ChartView;
class ChartCatalog;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openFolder();
    void onCursorMoved(double lon, double lat);
    void onScanProgress(int done, int total);
    void onScanFinished(bool ok, const QString& message);
    void onViewStatus(const QString& text);

private:
    void startScan(const QString& dir);

    ChartView*    view_ = nullptr;
    ChartCatalog* catalog_ = nullptr;
    QLabel*       statusLeft_ = nullptr;   // root folder + scan summary
    QLabel*       statusMid_ = nullptr;    // band / cells shown
    QLabel*       statusRight_ = nullptr;  // cursor lat/lon
    QString       root_;
};
