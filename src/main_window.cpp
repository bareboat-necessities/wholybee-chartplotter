#include "main_window.hpp"
#include "chart_view.hpp"
#include "chart_loader.hpp"

#include <QToolBar>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QApplication>
#include <memory>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Marine Chart Viewer"));
    resize(1100, 750);

    view_ = new ChartView(this);
    setCentralWidget(view_);
    connect(view_, &ChartView::cursorMoved, this, &MainWindow::onCursorMoved);

    QToolBar* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    QAction* openAct = tb->addAction(QStringLiteral("Open Chart Folder"));
    connect(openAct, &QAction::triggered, this, &MainWindow::openFolder);
    QAction* fitAct = tb->addAction(QStringLiteral("Fit"));
    connect(fitAct, &QAction::triggered, view_, &ChartView::fitToChart);

    statusLeft_  = new QLabel(QStringLiteral("No chart folder selected"));
    statusRight_ = new QLabel(QString());
    statusBar()->addWidget(statusLeft_, 1);
    statusBar()->addPermanentWidget(statusRight_);

    // Reopen the last-used folder, if it still exists.
    QSettings s;
    QString saved = s.value(QStringLiteral("charts/directory")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        loadFolder(saved);
}

void MainWindow::openFolder() {
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select ENC Chart Folder"),
        dir_.isEmpty() ? QString() : dir_);
    if (!dir.isEmpty())
        loadFolder(dir);
}

// NOTE: synchronous load (briefly blocks the UI on large sets). In Qt this is
// trivially moved off-thread, e.g.:
//   auto fut = QtConcurrent::run([dir]{ ... return chart; });
//   watcher.setFuture(fut);  // handle 'finished' on the GUI thread
void MainWindow::loadFolder(const QString& dir) {
    auto cs = std::make_shared<ChartSet>();
    std::string err;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = cs->loadDirectory(dir.toStdString(), err);
    QApplication::restoreOverrideCursor();

    if (ok) {
        dir_ = dir;
        view_->setChart(cs);
        QSettings().setValue(QStringLiteral("charts/directory"), dir);
        statusLeft_->setText(QStringLiteral("%1      %2 cell(s) \u00b7 %3 feature(s)")
                                 .arg(dir)
                                 .arg(cs->cellCount())
                                 .arg(cs->features().size()));
    } else {
        QMessageBox::warning(this, QStringLiteral("Could not load charts"),
                             QString::fromStdString(err));
    }
}

void MainWindow::onCursorMoved(double lon, double lat) {
    const QChar deg(0x00B0);
    const QString ns = (lat >= 0.0) ? QStringLiteral("N") : QStringLiteral("S");
    const QString ew = (lon >= 0.0) ? QStringLiteral("E") : QStringLiteral("W");
    statusRight_->setText(QString::number(std::fabs(lat), 'f', 4) + deg + ns
                          + QStringLiteral("   ")
                          + QString::number(std::fabs(lon), 'f', 4) + deg + ew);
}
