#include <QApplication>
#include "chart_loader.hpp"
#include "main_window.hpp"

int main(int argc, char** argv) {
    chart::init();   // GDAL drivers + S-57 options; before any worker threads

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ExampleMarine"));
    QApplication::setApplicationName(QStringLiteral("Marine Chart Viewer"));

    MainWindow w;
    w.show();
    return app.exec();
}
