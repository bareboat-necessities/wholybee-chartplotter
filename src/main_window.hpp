#pragma once
#include <QMainWindow>
#include <QString>

class ChartView;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openFolder();
    void onCursorMoved(double lon, double lat);

private:
    void loadFolder(const QString& dir);

    ChartView* view_ = nullptr;
    QLabel*    statusLeft_ = nullptr;
    QLabel*    statusRight_ = nullptr;
    QString    dir_;
};
