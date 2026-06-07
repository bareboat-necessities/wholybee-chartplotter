#include "main_window.hpp"
#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "settings.hpp"
#include "side_menu.hpp"
#include "chart_sets_dialog.hpp"
#include "units_dialog.hpp"
#include "stale_thresholds_dialog.hpp"
#include "nav_data_store.hpp"
#include "simulator.hpp"

#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QEvent>
#include <QCloseEvent>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Marine Chart Viewer"));
    resize(1100, 750);

    settings_ = new Settings(this);

    view_ = new ChartView(this);
    setCentralWidget(view_);
    connect(view_, &ChartView::cursorMoved,   this, &MainWindow::onCursorMoved);
    connect(view_, &ChartView::statusChanged, this, &MainWindow::onViewStatus);

    // Apply persisted display settings, then keep the view in sync with changes.
    view_->setShowSoundings(settings_->showSoundings());
    view_->setShowSymbols(settings_->showSymbols());
    view_->setShowDepthContours(settings_->showDepthContours());
    connect(settings_, &Settings::showSoundingsChanged,     view_, &ChartView::setShowSoundings);
    connect(settings_, &Settings::showSymbolsChanged,       view_, &ChartView::setShowSymbols);
    connect(settings_, &Settings::showDepthContoursChanged, view_, &ChartView::setShowDepthContours);

    // Depth unit drives how soundings are labelled.
    view_->setDepthUnit(settings_->depthUnit());
    connect(settings_, &Settings::depthUnitChanged, view_, &ChartView::setDepthUnit);

    // Ownship course-prediction length (minutes), persisted via Settings.
    view_->setOwnshipPredictionMinutes(settings_->ownshipPredictionMinutes());
    connect(settings_, &Settings::ownshipPredictionMinutesChanged,
            view_, &ChartView::setOwnshipPredictionMinutes);

    // Basemap underlay: load from the configured folder (or a standard location)
    // and keep it in sync if the user picks a different one.
    view_->setBasemapDirectory(settings_->basemapDirectory());
    connect(settings_, &Settings::basemapDirectoryChanged, view_, &ChartView::setBasemapDirectory);

    // Remember the pan/zoom location across runs: the view publishes its location
    // (debounced) and we persist it. Restoring happens only for the startup
    // auto-load below, so explicit chart-set switches still fit to the new set.
    connect(view_, &ChartView::viewChanged, settings_, &Settings::setView);

    catalog_ = new ChartCatalog(this);
    connect(catalog_, &ChartCatalog::progress, this, &MainWindow::onScanProgress);
    connect(catalog_, &ChartCatalog::finished, this, &MainWindow::onScanFinished);
    view_->setCatalog(catalog_);

    // Nav data store + simulator (built-in publisher). The store owns shared
    // ownship state; the view subscribes; the simulator publishes through the
    // INavDataPublisher API. This is the foundation for AIS/instruments/routes.
    navStore_  = new NavDataStore(this);
    navStore_->setStaleSeconds(settings_->staleSeconds());
    navStore_->setInvalidSeconds(settings_->invalidSeconds());
    connect(settings_, &Settings::staleThresholdsChanged,
            this, [this](double s, double i) {
        navStore_->setStaleSeconds(s);
        navStore_->setInvalidSeconds(i);
    });
    connect(navStore_, &NavDataStore::ownshipChanged,    this, &MainWindow::publishOwnshipToView);
    connect(navStore_, &NavDataStore::freshnessChanged,  this, [this](NavFreshness) { publishOwnshipToView(); });

    simulator_ = new Simulator(navStore_, this);
    simulator_->setPosition(settings_->simulatorLat(), settings_->simulatorLon());
    // Persist the simulator's position as it moves so it resumes where it left off.
    connect(simulator_, &Simulator::positionChanged,
            settings_, &Settings::setSimulatorPosition);
    connect(settings_, &Settings::simulatorEnabledChanged,
            simulator_, &Simulator::setRunning);
    if (settings_->simulatorEnabled()) simulator_->setRunning(true);

    // Touch-first navigation: a floating menu button over the chart opens the
    // side drawer. No toolbar, no right-click, large tap targets.
    sideMenu_ = new SideMenu(settings_, view_);
    connect(sideMenu_, &SideMenu::fitRequested,             view_, &ChartView::fitToCatalog);
    connect(sideMenu_, &SideMenu::chartSetSelected,         this,  &MainWindow::onChartSetSelected);
    connect(sideMenu_, &SideMenu::manageChartSetsRequested, this,  &MainWindow::manageChartSets);
    connect(sideMenu_, &SideMenu::basemapFolderRequested,   this,  &MainWindow::chooseBasemapFolder);
    connect(sideMenu_, &SideMenu::editUnitsRequested,       this,  &MainWindow::editUnits);
    connect(sideMenu_, &SideMenu::editStaleThresholdsRequested,     this, &MainWindow::editStaleThresholds);
    connect(sideMenu_, &SideMenu::editOwnshipPredictionRequested,   this, &MainWindow::editOwnshipPrediction);

    menuButton_ = new QPushButton(QStringLiteral("☰"), view_);  // hamburger
    menuButton_->setFixedSize(48, 48);
    menuButton_->setCursor(Qt::PointingHandCursor);
    menuButton_->setStyleSheet(QStringLiteral(
        "QPushButton{ font-size:22px; border:1px solid #b0b0b0; border-radius:6px;"
        " background:rgba(255,255,255,0.92); }"
        "QPushButton:pressed{ background:#dce6f0; }"));
    connect(menuButton_, &QPushButton::clicked, sideMenu_, &SideMenu::openMenu);
    menuButton_->show();
    view_->installEventFilter(this);   // reposition the button when the view resizes
    positionMenuButton();

    statusLeft_  = new QLabel(QStringLiteral("No chart folder selected"));
    statusMid_   = new QLabel(QString());
    statusRight_ = new QLabel(QString());
    statusBar()->addWidget(statusLeft_, 1);
    statusBar()->addPermanentWidget(statusMid_);
    statusBar()->addPermanentWidget(statusRight_);

    const QString saved = settings_->chartDirectory();
    if (!saved.isEmpty() && QDir(saved).exists()) {
        if (settings_->hasSavedView())
            view_->setInitialView(settings_->viewLon(), settings_->viewLat(),
                                  settings_->viewScale());
        startScan(saved);
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == view_ && e->type() == QEvent::Resize)
        positionMenuButton();
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    view_->persistViewNow();   // flush the latest location even if mid-debounce
    QMainWindow::closeEvent(e);
}

void MainWindow::positionMenuButton() {
    if (!menuButton_) return;
    menuButton_->move(12, 12);
    if (!sideMenu_ || !sideMenu_->isOpen())
        menuButton_->raise();   // stay above the chart, but never above an open menu
}

void MainWindow::onChartSetSelected(const QString& dir) {
    // Tapping a set loads it; tapping the active set again re-scans it.
    settings_->setChartDirectory(dir);
    startScan(dir);
}

void MainWindow::manageChartSets() {
    const bool hadActive = !settings_->chartDirectory().isEmpty();
    ChartSetsDialog dlg(settings_->chartSets(), this);
    if (dlg.exec() == QDialog::Accepted) {
        settings_->setChartSets(dlg.chartSets());
        // If there was no active set before and the user just added the first
        // one, activate it automatically so they don't close the menu to an
        // empty chart with no indication of what to do next.
        if (!hadActive && !dlg.chartSets().isEmpty())
            onChartSetSelected(dlg.chartSets().first().directory);
    }
}

void MainWindow::chooseBasemapFolder() {
    const QString start = settings_->basemapDirectory();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select GSHHG Basemap Folder (contains GSHHS_shp)"), start);
    if (!dir.isEmpty())
        settings_->setBasemapDirectory(dir);
}

void MainWindow::editUnits() {
    UnitsDialog dlg(settings_->depthUnit(), settings_->distanceUnit(), this);
    if (dlg.exec() == QDialog::Accepted) {
        settings_->setDepthUnit(dlg.depthUnit());
        settings_->setDistanceUnit(dlg.distanceUnit());
    }
}

void MainWindow::editStaleThresholds() {
    StaleThresholdsDialog dlg(settings_->staleSeconds(), settings_->invalidSeconds(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setStaleThresholds(dlg.staleSeconds(), dlg.invalidSeconds());
}

void MainWindow::editOwnshipPrediction() {
    bool ok = false;
    const double m = QInputDialog::getDouble(
        this, QStringLiteral("Ownship Course Prediction"),
        QStringLiteral("Length of the course prediction line (minutes):"),
        settings_->ownshipPredictionMinutes(), 0.5, 120.0, 1, &ok);
    if (!ok) return;
    settings_->setOwnshipPredictionMinutes(m);
}

void MainWindow::publishOwnshipToView() {
    if (view_) view_->setOwnship(navStore_->ownship(), navStore_->freshness());
}

void MainWindow::startScan(const QString& dir) {
    if (catalog_->isScanning()) return;
    root_ = dir;
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
