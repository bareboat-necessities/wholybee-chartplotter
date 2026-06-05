#include <QApplication>
#include <gdal.h>
#include "main_window.hpp"

int main(int argc, char** argv) {
    GDALAllRegister();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ExampleMarine"));
    QApplication::setApplicationName(QStringLiteral("Marine Chart Viewer"));

    MainWindow w;
    w.show();
    return app.exec();
}
