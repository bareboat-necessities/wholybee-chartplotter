#include "main_window.hpp"
#include "chart_view.hpp"
#include "chart_catalog.hpp"
#include "theme.hpp"
#include "settings.hpp"
#include "side_menu.hpp"
#include "chart_sets_dialog.hpp"
#include "units_dialog.hpp"
#include "stale_thresholds_dialog.hpp"
#include "ownship_prediction_dialog.hpp"
#include "nav_data_browser_window.hpp"
#include "data_priority_dialog.hpp"
#include "nav_data_store.hpp"
#include "ais_target_store.hpp"
#include "ais_overlay.hpp"
#include "ais_target_info_window.hpp"
#include "simulator.hpp"
#include "core_api.hpp"
#include "plugin_manager.hpp"
#include "nmea0183_plugin.hpp"
#include "test_plugin.hpp"

#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QEvent>
#include <QCloseEvent>
#include <QSettings>
#include <cmath>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Marine Chart Viewer"));
    resize(1100, 750);   // first-run default; overridden by restoreGeometry below

    // Restore the previous size / position / maximised state (no-op on first run
    // or when the saved screen no longer exists — Qt rejects an off-screen rect).
    const QByteArray geom =
        QSettings().value(QStringLiteral("window/geometry")).toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);

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

    // Depth unit drives how soundings are labelled; distance unit drives the
    // scale bar.
    view_->setDepthUnit(settings_->depthUnit());
    connect(settings_, &Settings::depthUnitChanged, view_, &ChartView::setDepthUnit);
    view_->setDistanceUnit(settings_->distanceUnit());
    connect(settings_, &Settings::distanceUnitChanged, view_, &ChartView::setDistanceUnit);

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
    // Source arbitration: highest-priority source wins, falling back when its
    // data goes invalid. Sources register into registry_ (NMEA via its plugin,
    // simulator below); the saved order is applied after plugin init (below).
    connect(settings_, &Settings::dataSourcePriorityChanged,
            navStore_, &NavDataStore::setSourcePriority);
    // ownshipChanged fires on new data and on any per-value freshness transition.
    connect(navStore_, &NavDataStore::ownshipChanged, this, &MainWindow::publishOwnshipToView);

    // AIS target store: keyed by MMSI, fed by the NMEA 0183 plugin's AIS decoder
    // via IAisPublisher; consumers subscribe to it. Stale at 6 min, lost at 12.
    aisStore_ = new AisTargetStore(this);

    // AIS chart overlay: green vessel glyphs (same shape as ownship, predictor
    // line + cancellation slash when stale). Kept in step with ownship's
    // configurable predictor length, and triggers a repaint as targets change.
    aisOverlay_ = std::make_unique<AisOverlay>(aisStore_);
    aisOverlay_->setPredictionMinutes(settings_->ownshipPredictionMinutes());
    aisOverlay_->setOnTargetClicked([this](quint32 mmsi) {
        // Click on a target opens (or raises) an info window for that MMSI.
        // The window deletes itself on close; QPointer drops the stale entry.
        AisTargetInfoWindow* w = aisInfoWindows_.value(mmsi);
        if (!w) {
            w = new AisTargetInfoWindow(mmsi, aisStore_, this);
            w->setAttribute(Qt::WA_DeleteOnClose);
            aisInfoWindows_.insert(mmsi, w);
        }
        w->show();
        w->raise();
        w->activateWindow();
    });
    view_->addOverlay(aisOverlay_.get());
    connect(settings_, &Settings::ownshipPredictionMinutesChanged,
            this, [this](double m) {
        if (aisOverlay_) aisOverlay_->setPredictionMinutes(m);
        if (view_) view_->update();
    });
    connect(aisStore_, &AisTargetStore::targetUpdated, this, [this](quint32) {
        if (view_) view_->update();
    });
    connect(aisStore_, &AisTargetStore::targetExpired, this, [this](quint32) {
        if (view_) view_->update();
    });

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
    // Auto-hide behaviour: drives both tap-outside dismiss and action auto-close.
    sideMenu_->setAutoHide(settings_->autoHideMenu());
    connect(settings_, &Settings::autoHideMenuChanged,
            sideMenu_, &SideMenu::setAutoHide);
    connect(sideMenu_, &SideMenu::fitRequested,             view_, &ChartView::fitToCatalog);
    connect(sideMenu_, &SideMenu::centerOnOwnshipRequested, view_, &ChartView::centerOnOwnship);
    connect(sideMenu_, &SideMenu::autoFollowToggled,        view_, &ChartView::setAutoFollow);
    connect(view_, &ChartView::autoFollowChanged,           sideMenu_, &SideMenu::setAutoFollowChecked);
    connect(sideMenu_, &SideMenu::chartSetSelected,         this,  &MainWindow::onChartSetSelected);
    connect(sideMenu_, &SideMenu::manageChartSetsRequested, this,  &MainWindow::manageChartSets);
    connect(sideMenu_, &SideMenu::basemapFolderRequested,   this,  &MainWindow::chooseBasemapFolder);
    connect(sideMenu_, &SideMenu::editUnitsRequested,       this,  &MainWindow::editUnits);
    connect(sideMenu_, &SideMenu::editStaleThresholdsRequested,     this, &MainWindow::editStaleThresholds);
    connect(sideMenu_, &SideMenu::editOwnshipPredictionRequested,   this, &MainWindow::editOwnshipPrediction);
    connect(sideMenu_, &SideMenu::navDataBrowserRequested,          this, &MainWindow::showNavDataBrowser);
    connect(sideMenu_, &SideMenu::editDataPriorityRequested,        this, &MainWindow::editDataPriority);

    // Plugin layer: the core exposes services through CoreApi; the manager owns
    // the built-in plugins and drives their lifecycle. Same interfaces a dynamic
    // plugin would use later. NMEA 0183 is a plugin; the test plugin exercises
    // menus, overlays, and the nav data API. Plugins register their sources here.
    coreApi_ = std::make_unique<CoreApi>(navStore_, aisStore_, sideMenu_, view_, &registry_, this);
    plugins_ = std::make_unique<PluginManager>(coreApi_.get());
    plugins_->add(std::make_unique<Nmea0183Plugin>());   // first => default-highest priority
    plugins_->add(std::make_unique<TestPlugin>());
    plugins_->initializeAll();

    // Simulator is a core built-in source; register it last so real data
    // (NMEA) outranks it by default. Then apply the saved priority order.
    registry_.add(QStringLiteral("simulator"), QStringLiteral("Simulator"));
    navStore_->setSourcePriority(registry_.orderedIds(settings_->dataSourcePriority()));

    menuButton_ = new QPushButton(QStringLiteral("☰"), view_);  // hamburger
    menuButton_->setFixedSize(48, 48);
    menuButton_->setCursor(Qt::PointingHandCursor);
    // Pin colour explicitly: the hamburger glyph otherwise inherits the system
    // text colour (white in dark mode) which becomes invisible on a translucent
    // light background. The overlayBtn palette swaps for the OS theme.
    const theme::OverlayBtnPalette& ob = theme::overlayBtn();
    menuButton_->setStyleSheet(QStringLiteral(
        "QPushButton{ font-size:22px; color:%1; border:1px solid %2;"
        " border-radius:6px; background:%3; }"
        "QPushButton:pressed{ background:%4; }")
        .arg(ob.fg, ob.border, ob.bg, ob.pressed));
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

// Defined here (not =default in the header) so CoreApi/PluginManager are complete
// types when the unique_ptr members are destroyed. The manager shuts plugins down
// (removing overlays from the still-alive ChartView) before they are freed.
MainWindow::~MainWindow() = default;

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == view_ && e->type() == QEvent::Resize)
        positionMenuButton();
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    view_->persistViewNow();   // flush the latest location even if mid-debounce
    // Persist size / position / maximised state for the next launch.
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
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
    OwnshipPredictionDialog dlg(settings_->ownshipPredictionMinutes(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setOwnshipPredictionMinutes(dlg.minutes());
}

void MainWindow::showNavDataBrowser() {
    if (!navBrowser_)
        navBrowser_ = new NavDataBrowserWindow(navStore_, this);
    navBrowser_->show();
    navBrowser_->raise();
    navBrowser_->activateWindow();
}

void MainWindow::editDataPriority() {
    // Show all registered sources (built-in + plugin) in the saved order.
    DataPriorityDialog dlg(registry_.ordered(settings_->dataSourcePriority()), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setDataSourcePriority(dlg.orderedIds());
}

void MainWindow::publishOwnshipToView() {
    if (view_) view_->setOwnship(navStore_->ownship());
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
