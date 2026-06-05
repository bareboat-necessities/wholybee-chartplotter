#include "main_window.hpp"
#include "chart_view.hpp"
#include "chart_catalog.hpp"

#include <QToolBar>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Marine Chart Viewer"));
    resize(1100, 750);

    view_ = new ChartView(this);
    setCentralWidget(view_);
    connect(view_, &ChartView::cursorMoved,  this, &MainWindow::onCursorMoved);
    connect(view_, &ChartView::statusChanged, this, &MainWindow::onViewStatus);

    catalog_ = new ChartCatalog(this);
    connect(catalog_, &ChartCatalog::progress, this, &MainWindow::onScanProgress);
    connect(catalog_, &ChartCatalog::finished, this, &MainWindow::onScanFinished);
    view_->setCatalog(catalog_);

    QToolBar* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    QAction* openAct = tb->addAction(QStringLiteral("Open Chart Folder"));
    connect(openAct, &QAction::triggered, this, &MainWindow::openFolder);
    QAction* fitAct = tb->addAction(QStringLiteral("Fit"));
    connect(fitAct, &QAction::triggered, view_, &ChartView::fitToCatalog);

    statusLeft_  = new QLabel(QStringLiteral("No chart folder selected"));
    statusMid_   = new QLabel(QString());
    statusRight_ = new QLabel(QString());
    statusBar()->addWidget(statusLeft_, 1);
    statusBar()->addPermanentWidget(statusMid_);
    statusBar()->addPermanentWidget(statusRight_);

    QSettings s;
    QString saved = s.value(QStringLiteral("charts/directory")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        startScan(saved);
}

void MainWindow::openFolder() {
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select ENC Chart Root Folder"),
        root_.isEmpty() ? QString() : root_);
    if (!dir.isEmpty())
        startScan(dir);
}

void MainWindow::startScan(const QString& dir) {
    if (catalog_->isScanning()) return;
    root_ = dir;
    QSettings().setValue(QStringLiteral("charts/directory"), dir);
    statusLeft_->setText(dir + QStringLiteral("   —   scanning…"));
    statusMid_->clear();
    catalog_->startScan(dir);
}

void MainWindow::onScanProgress(int done, int total) {
    statusLeft_->setText(root_ + QStringLiteral("   —   cataloging %1 / %2").arg(done).arg(total));
}

void MainWindow::onScanFinished(bool ok, const QString& message) {
    if (ok) {
        statusLeft_->setText(root_ + QStringLiteral("   —   ") + message);
    } else {
        statusLeft_->setText(QStringLiteral("No chart folder selected"));
        QMessageBox::warning(this, QStringLiteral("Could not catalog charts"), message);
    }
}

void MainWindow::onViewStatus(const QString& text) {
    statusMid_->setText(text);
}

void MainWindow::onCursorMoved(double lon, double lat) {
    const QChar deg(0x00B0);
    const QString ns = (lat >= 0.0) ? QStringLiteral("N") : QStringLiteral("S");
    const QString ew = (lon >= 0.0) ? QStringLiteral("E") : QStringLiteral("W");
    statusRight_->setText(QString::number(std::fabs(lat), 'f', 4) + deg + ns
                          + QStringLiteral("   ")
                          + QString::number(std::fabs(lon), 'f', 4) + deg + ew);
}
