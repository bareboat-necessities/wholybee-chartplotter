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
#include "chart_detail_dialog.hpp"
#include "chart_symbol_size_dialog.hpp"
#include "ship_size_dialog.hpp"
#include "ownship_mmsi_dialog.hpp"
#include "heading_source_dialog.hpp"
#include "dangerous_ships_dialog.hpp"
#include "ais_target_list_dialog.hpp"
#include "route_store.hpp"
#include "route_overlay.hpp"
#include "route_list_dialog.hpp"
#include "waypoint_list_dialog.hpp"
#include "name_dialog.hpp"
#include "nav_data_store.hpp"
#include "ais_target_store.hpp"
#include "cpa_calculator.hpp"
#include "ais_overlay.hpp"
#include "ais_target_info_window.hpp"
#include "ais_quick_info_window.hpp"
#include "simulator.hpp"
#include "core_api.hpp"
#include "plugin_manager.hpp"
#include "nmea0183_plugin.hpp"
#include "nmea2000_plugin.hpp"

#include <QCoreApplication>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QEvent>
#include <QCloseEvent>
#include <QSettings>
#include <QCursor>
#include <algorithm>
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
    view_->setShowRasterCharts(settings_->showRasterCharts());
    view_->setHideSymbolsWhilePanning(settings_->hideSymbolsWhilePanning());
    connect(settings_, &Settings::showSoundingsChanged,     view_, &ChartView::setShowSoundings);
    connect(settings_, &Settings::showSymbolsChanged,       view_, &ChartView::setShowSymbols);
    connect(settings_, &Settings::showDepthContoursChanged, view_, &ChartView::setShowDepthContours);
    connect(settings_, &Settings::showRasterChartsChanged,  view_, &ChartView::setShowRasterCharts);
    connect(settings_, &Settings::hideSymbolsWhilePanningChanged,
            view_, &ChartView::setHideSymbolsWhilePanning);
    connect(view_, &ChartView::rasterChartsChanged, this, &MainWindow::onRasterChartsChanged);
    view_->setChartDetailLevel(settings_->chartDetailLevel());
    connect(settings_, &Settings::chartDetailLevelChanged,
            view_, &ChartView::setChartDetailLevel);
    view_->setSymbolScale(settings_->symbolScale());
    connect(settings_, &Settings::symbolScaleChanged,
            view_, &ChartView::setSymbolScale);
    view_->setVesselScale(settings_->vesselScale());
    connect(settings_, &Settings::vesselScaleChanged,
            view_, &ChartView::setVesselScale);
    view_->setHeadingSource(settings_->headingSource());
    connect(settings_, &Settings::headingSourceChanged,
            view_, &ChartView::setHeadingSource);

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
    aisOverlay_ = std::make_unique<AisOverlay>(aisStore_, navStore_);
    aisOverlay_->setPredictionMinutes(settings_->ownshipPredictionMinutes());
    aisOverlay_->setVesselScale(settings_->vesselScale());
    aisOverlay_->setVisible(settings_->showAisTargets());
    aisStore_->setStaleSeconds(settings_->aisStaleSeconds());
    aisStore_->setLostSeconds(settings_->aisLostSeconds());
    aisOverlay_->setOnTargetClicked([this](quint32 mmsi) { showAisTarget(mmsi); });
    view_->addOverlay(aisOverlay_.get());
    // Any chart interaction (empty click, pan, zoom) dismisses the quick-look
    // popup; the full info window is unaffected.
    connect(view_, &ChartView::chartInteracted, this, [this] {
        if (aisQuickInfo_) aisQuickInfo_->close();
        aisQuickInfoMmsi_ = 0;
    });
    connect(settings_, &Settings::ownshipPredictionMinutesChanged,
            this, [this](double m) {
        if (aisOverlay_) aisOverlay_->setPredictionMinutes(m);
        if (view_) view_->update();
    });
    connect(settings_, &Settings::vesselScaleChanged,
            this, [this](double s) {
        if (aisOverlay_) aisOverlay_->setVesselScale(s);
    });
    connect(settings_, &Settings::showAisTargetsChanged,
            this, [this](bool on) {
        if (aisOverlay_) aisOverlay_->setVisible(on);
        if (view_) view_->update();
    });
    // Dangerous-ship rules: push the current values into the overlay and keep
    // them in sync; a change repaints so flags update immediately.
    auto applyDangerRules = [this] {
        if (!aisOverlay_) return;
        DangerRules r;
        r.ignoreFarEnabled = settings_->dangerIgnoreFarEnabled();
        r.ignoreFarNm      = settings_->dangerIgnoreFarNm();
        r.cpaEnabled  = settings_->dangerCpaEnabled();
        r.cpaNm       = settings_->dangerCpaNm();
        r.tcpaEnabled = settings_->dangerTcpaEnabled();
        r.tcpaMin     = settings_->dangerTcpaMin();
        r.anchoredSafeEnabled = settings_->dangerAnchoredSafeEnabled();
        r.anchoredSogKn       = settings_->dangerAnchoredSogKn();
        aisOverlay_->setDangerRules(r);
        if (view_) view_->update();
    };
    applyDangerRules();
    connect(settings_, &Settings::dangerousShipsChanged, this, applyDangerRules);
    connect(settings_, &Settings::aisStaleThresholdsChanged,
            this, [this](double staleS, double lostS) {
        if (aisStore_) {
            aisStore_->setStaleSeconds(staleS);
            aisStore_->setLostSeconds(lostS);
        }
    });
    connect(aisStore_, &AisTargetStore::targetUpdated, this, [this](quint32) {
        if (view_) view_->update();
    });
    connect(aisStore_, &AisTargetStore::targetExpired, this, [this](quint32) {
        if (view_) view_->update();
    });

    // Routes & waypoints: SQLite-backed store + chart overlay/editor. The overlay
    // is added after the AIS overlay so that, during an edit session, its
    // hitTest gets first refusal on a tap (reverse z-order) and can add/select
    // route nodes even over an AIS target. Store changes repaint the chart.
    routeStore_ = new RouteStore(this);
    routeOverlay_ = std::make_unique<RouteOverlay>(routeStore_);
    routeOverlay_->setRepaintCallback([this] { if (view_) view_->update(); });
    routeOverlay_->setWaypointPlacedCallback([this](double lat, double lon) {
        onWaypointPlaced(lat, lon);
    });
    routeOverlay_->setSelectionChangedCallback([this](bool has) {
        if (deletePointBtn_) deletePointBtn_->setEnabled(has);
    });
    view_->addOverlay(routeOverlay_.get());
    connect(routeStore_, &RouteStore::routesChanged,    this, [this] { if (view_) view_->update(); });
    connect(routeStore_, &RouteStore::waypointsChanged, this, [this] { if (view_) view_->update(); });

    // Collision component: computes each target's CPA/TCPA against the ownship
    // and writes them back into the store (which the overlay and info windows
    // read). Skips the boat's own MMSI so its AIS echo never alarms on itself.
    cpaCalc_ = new CpaCalculator(navStore_, aisStore_, this);
    cpaCalc_->setOwnshipMmsi(settings_->ownshipMmsi().toUInt());
    connect(settings_, &Settings::ownshipMmsiChanged, this, [this](const QString& m) {
        if (cpaCalc_) cpaCalc_->setOwnshipMmsi(m.toUInt());
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
    connect(sideMenu_, &SideMenu::editChartDetailLevelRequested,    this, &MainWindow::editChartDetailLevel);
    connect(sideMenu_, &SideMenu::editSymbolSizeRequested,          this, &MainWindow::editSymbolSize);
    connect(sideMenu_, &SideMenu::editVesselSizeRequested,          this, &MainWindow::editVesselSize);
    connect(sideMenu_, &SideMenu::editOwnshipMmsiRequested,         this, &MainWindow::editOwnshipMmsi);
    connect(sideMenu_, &SideMenu::editHeadingSourceRequested,       this, &MainWindow::editHeadingSource);
    connect(sideMenu_, &SideMenu::editDangerousShipsRequested,      this, &MainWindow::editDangerousShips);
    connect(sideMenu_, &SideMenu::aisTargetListRequested,           this, &MainWindow::showAisTargetList);
    connect(sideMenu_, &SideMenu::createRouteRequested,    this, &MainWindow::startCreateRoute);
    connect(sideMenu_, &SideMenu::editRouteRequested,      this, &MainWindow::startEditRoute);
    connect(sideMenu_, &SideMenu::routeListRequested,      this, &MainWindow::showRouteList);
    connect(sideMenu_, &SideMenu::createWaypointRequested, this, &MainWindow::startCreateWaypoint);
    connect(sideMenu_, &SideMenu::editWaypointRequested,   this, &MainWindow::startEditWaypoint);
    connect(sideMenu_, &SideMenu::dropWaypointRequested,   this, &MainWindow::dropWaypoint);
    connect(sideMenu_, &SideMenu::waypointListRequested,   this, &MainWindow::showWaypointList);

    buildEditBar();   // floating Complete/Delete/Cancel bar (hidden until editing)

    // Plugin layer: the core exposes services through CoreApi; the manager owns
    // the built-in plugins and drives their lifecycle. Same interfaces a dynamic
    // plugin would use later. NMEA 0183 is a plugin; the test plugin exercises
    // menus, overlays, and the nav data API. Plugins register their sources here.
    coreApi_ = std::make_unique<CoreApi>(navStore_, aisStore_, sideMenu_, view_, &registry_, this);
    plugins_ = std::make_unique<PluginManager>(coreApi_.get());
    plugins_->add(std::make_unique<Nmea0183Plugin>());   // first => default-highest priority
    plugins_->add(std::make_unique<Nmea2000Plugin>());
    // Dynamic plugins discovered alongside the exe. Test Plugin lives here
    // now — built as its own VS project under plugins/test_plugin/, loaded
    // via QPluginLoader.
    plugins_->loadFromDirectory(QCoreApplication::applicationDirPath()
                                + QStringLiteral("/plugins"));
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
    if (obj == view_ && e->type() == QEvent::Resize) {
        positionMenuButton();
        positionEditBar();
    }
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
    StaleThresholdsDialog dlg(settings_->staleSeconds(), settings_->invalidSeconds(),
                              settings_->aisStaleSeconds(), settings_->aisLostSeconds(),
                              this);
    if (dlg.exec() == QDialog::Accepted) {
        settings_->setStaleThresholds(dlg.staleSeconds(), dlg.invalidSeconds());
        settings_->setAisStaleThresholds(dlg.aisStaleSeconds(), dlg.aisLostSeconds());
    }
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

void MainWindow::editChartDetailLevel() {
    ChartDetailDialog dlg(settings_->chartDetailLevel(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setChartDetailLevel(dlg.detailLevel());
}

void MainWindow::editSymbolSize() {
    ChartSymbolSizeDialog dlg(settings_->symbolScale(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setSymbolScale(dlg.symbolScale());
}

void MainWindow::editVesselSize() {
    ShipSizeDialog dlg(settings_->vesselScale(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setVesselScale(dlg.vesselScale());
}

void MainWindow::editOwnshipMmsi() {
    OwnshipMmsiDialog dlg(settings_->ownshipMmsi(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setOwnshipMmsi(dlg.mmsi());
}

void MainWindow::editHeadingSource() {
    HeadingSourceDialog dlg(settings_->headingSource(), this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setHeadingSource(dlg.source());
}

void MainWindow::editDangerousShips() {
    DangerousShipsDialog dlg(settings_->dangerIgnoreFarEnabled(), settings_->dangerIgnoreFarNm(),
                             settings_->dangerCpaEnabled(), settings_->dangerCpaNm(),
                             settings_->dangerTcpaEnabled(), settings_->dangerTcpaMin(),
                             settings_->dangerAnchoredSafeEnabled(), settings_->dangerAnchoredSogKn(),
                             this);
    if (dlg.exec() == QDialog::Accepted)
        settings_->setDangerousShips(dlg.ignoreFarEnabled(), dlg.ignoreFarNm(),
                                     dlg.cpaEnabled(), dlg.cpaNm(),
                                     dlg.tcpaEnabled(), dlg.tcpaMin(),
                                     dlg.anchoredSafeEnabled(), dlg.anchoredSogKn());
}

// ---- Routes & Waypoints ----------------------------------------------------

void MainWindow::buildEditBar() {
    // A compact floating toolbar over the chart, used while creating/editing a
    // route or placing a waypoint. Child of the view so it overlays the chart.
    editBar_ = new QWidget(view_);
    editBar_->setStyleSheet(QStringLiteral(
        "QWidget{ background: rgba(30,34,40,235); border:1px solid rgba(255,255,255,40);"
        " border-radius:8px; }"
        "QLabel{ color:#e6e9ee; font-size:13px; background:transparent; border:none; }"
        "QPushButton{ font-size:14px; min-height:38px; padding:0 14px;"
        " color:#ffffff; border:1px solid rgba(255,255,255,60); border-radius:6px;"
        " background: rgba(255,255,255,16); }"
        "QPushButton:disabled{ color: rgba(255,255,255,90); }"
        "QPushButton:pressed{ background: rgba(255,255,255,40); }"));
    auto* row = new QHBoxLayout(editBar_);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(8);

    editHint_ = new QLabel(editBar_);
    row->addWidget(editHint_);
    row->addSpacing(4);

    deletePointBtn_ = new QPushButton(QStringLiteral("Delete Point"), editBar_);
    deletePointBtn_->setEnabled(false);
    connect(deletePointBtn_, &QPushButton::clicked, this, [this] {
        if (routeOverlay_) routeOverlay_->deleteSelectedNode();
    });
    row->addWidget(deletePointBtn_);

    completeBtn_ = new QPushButton(QStringLiteral("Complete Route"), editBar_);
    connect(completeBtn_, &QPushButton::clicked, this, &MainWindow::completeEdit);
    row->addWidget(completeBtn_);

    cancelEditBtn_ = new QPushButton(QStringLiteral("Cancel"), editBar_);
    connect(cancelEditBtn_, &QPushButton::clicked, this, &MainWindow::endRouteMode);
    row->addWidget(cancelEditBtn_);

    editBar_->hide();
}

void MainWindow::positionEditBar() {
    if (!editBar_ || !editBar_->isVisible() || !view_) return;
    editBar_->adjustSize();
    const int x = (view_->width() - editBar_->width()) / 2;
    editBar_->move(std::max(8, x), 12);
    editBar_->raise();
}

void MainWindow::showRouteEditBar(const QString& hint) {
    editHint_->setText(hint);
    completeBtn_->setText(QStringLiteral("Complete Route"));
    deletePointBtn_->show();
    completeBtn_->show();
    deletePointBtn_->setEnabled(false);
    editBar_->show();
    positionEditBar();
}

void MainWindow::showWaypointPlaceBar(const QString& hint) {
    editHint_->setText(hint);
    deletePointBtn_->hide();
    completeBtn_->hide();
    editBar_->show();
    positionEditBar();
}

void MainWindow::showWaypointEditBar(const QString& hint) {
    editHint_->setText(hint);
    completeBtn_->setText(QStringLiteral("Done"));
    deletePointBtn_->hide();
    completeBtn_->show();
    editBar_->show();
    positionEditBar();
}

void MainWindow::endRouteMode() {
    if (routeOverlay_) routeOverlay_->endEditing();
    if (view_) view_->setChartEditor(nullptr);
    if (editBar_) editBar_->hide();
    if (view_) view_->update();
}

void MainWindow::startCreateRoute() {
    if (sideMenu_) sideMenu_->closeMenu();
    routeOverlay_->beginCreateRoute();
    view_->setChartEditor(routeOverlay_.get());
    showRouteEditBar(QStringLiteral("Tap the chart to add points · drag to move · tap a point to select"));
}

void MainWindow::startEditRoute() {
    if (!routeStore_) return;
    // Modal picker. A row tap emits routePicked and accepts; we then begin edit.
    RouteListDialog dlg(routeStore_, /*pickMode=*/true, this);
    qint64 picked = -1;
    connect(&dlg, &RouteListDialog::routePicked, this, [&picked](qint64 id) { picked = id; });
    if (dlg.exec() == QDialog::Accepted && picked >= 0) beginEditRoute(picked);
}

void MainWindow::beginEditRoute(qint64 id) {
    const Route* r = routeStore_->route(id);
    if (!r || r->points.isEmpty()) return;
    // Frame the route, then enter edit mode on a working copy.
    double latMin = 90, latMax = -90, lonMin = 180, lonMax = -180;
    for (const RoutePoint& p : r->points) {
        latMin = std::min(latMin, p.lat); latMax = std::max(latMax, p.lat);
        lonMin = std::min(lonMin, p.lon); lonMax = std::max(lonMax, p.lon);
    }
    view_->fitToGeoBox(latMin, lonMin, latMax, lonMax);
    if (sideMenu_) sideMenu_->closeMenu();
    routeOverlay_->beginEditRoute(*r);
    view_->setChartEditor(routeOverlay_.get());
    showRouteEditBar(QStringLiteral("Drag to move · tap empty chart to add · select a point to delete"));
}

void MainWindow::completeEdit() {
    // The single Complete/Done button serves both route and waypoint editing.
    if (!routeOverlay_) return;
    if (routeOverlay_->mode() == RouteOverlay::Mode::EditWaypoint)
        completeWaypointMove();
    else
        completeRoute();
}

void MainWindow::completeRoute() {
    if (!routeOverlay_ || !routeStore_) return;
    Route r = routeOverlay_->workingRoute();
    if (r.points.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Route"),
            QStringLiteral("A route needs at least two points."));
        return;
    }
    NameDialog dlg(QStringLiteral("Name Route"), r.name, r.description, this);
    if (dlg.exec() != QDialog::Accepted) return;   // keep editing on cancel
    r.name = dlg.name();
    r.description = dlg.description();
    if (r.id < 0) routeStore_->addRoute(r);
    else          routeStore_->updateRoute(r);
    endRouteMode();
}

void MainWindow::startCreateWaypoint() {
    if (sideMenu_) sideMenu_->closeMenu();
    routeOverlay_->beginCreateWaypoint();
    view_->setChartEditor(routeOverlay_.get());
    showWaypointPlaceBar(QStringLiteral("Tap the chart to place a waypoint"));
}

void MainWindow::onWaypointPlaced(double lat, double lon) {
    // Placing ends the create-waypoint mode; then name & save it.
    endRouteMode();
    NameDialog dlg(QStringLiteral("Name Waypoint"), QString(), QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;   // cancel discards the point
    Waypoint w;
    w.name = dlg.name();
    w.description = dlg.description();
    w.lat = lat; w.lon = lon;
    w.visible = true;
    routeStore_->addWaypoint(w);
}

void MainWindow::startEditWaypoint() {
    if (!routeStore_) return;
    WaypointListDialog dlg(routeStore_, /*pickMode=*/true, this);
    qint64 picked = -1;
    connect(&dlg, &WaypointListDialog::waypointPicked, this, [&picked](qint64 id) { picked = id; });
    if (dlg.exec() == QDialog::Accepted && picked >= 0) beginEditWaypoint(picked);
}

void MainWindow::beginEditWaypoint(qint64 id) {
    const Waypoint* found = nullptr;
    for (const Waypoint& w : routeStore_->waypoints())
        if (w.id == id) { found = &w; break; }
    if (!found) return;
    const Waypoint w = *found;   // copy before any repaint touches the store
    view_->fitToGeoBox(w.lat, w.lon, w.lat, w.lon);   // single point: padded by fitToGeoBox
    if (sideMenu_) sideMenu_->closeMenu();
    routeOverlay_->beginEditWaypoint(w);
    view_->setChartEditor(routeOverlay_.get());
    showWaypointEditBar(QStringLiteral("Drag the waypoint to move it, then Done"));
}

void MainWindow::completeWaypointMove() {
    if (!routeOverlay_ || !routeStore_) return;
    routeStore_->updateWaypoint(routeOverlay_->workingWaypoint());
    endRouteMode();
}

void MainWindow::dropWaypoint() {
    if (sideMenu_) sideMenu_->closeMenu();
    const OwnshipState& s = navStore_->ownship();
    if (!s.latitudeDeg.valid() || !s.longitudeDeg.valid()) {
        statusLeft_->setText(QStringLiteral("Drop Waypoint: no ownship position available"));
        return;
    }
    NameDialog dlg(QStringLiteral("Drop Waypoint"), QString(), QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    Waypoint w;
    w.name = dlg.name();
    w.description = dlg.description();
    w.lat = s.latitudeDeg.value;
    w.lon = s.longitudeDeg.value;
    w.visible = true;
    routeStore_->addWaypoint(w);
}

void MainWindow::showRouteList() {
    if (!routeListDlg_) {
        routeListDlg_ = new RouteListDialog(routeStore_, /*pickMode=*/false, this);
        routeListDlg_->setAttribute(Qt::WA_DeleteOnClose);
    }
    routeListDlg_->show();
    routeListDlg_->raise();
    routeListDlg_->activateWindow();
}

void MainWindow::showWaypointList() {
    if (!waypointListDlg_) {
        waypointListDlg_ = new WaypointListDialog(routeStore_, /*pickMode=*/false, this);
        waypointListDlg_->setAttribute(Qt::WA_DeleteOnClose);
    }
    waypointListDlg_->show();
    waypointListDlg_->raise();
    waypointListDlg_->activateWindow();
}

void MainWindow::showAisTargetList() {
    // Modeless: one instance, raised on subsequent opens; QPointer clears when
    // the dialog self-deletes so the next open creates a fresh one.
    if (!aisListDlg_) {
        aisListDlg_ = new AisTargetListDialog(aisStore_, this);
        aisListDlg_->setAttribute(Qt::WA_DeleteOnClose);
        // Tapping a row opens (or raises) the full info window for that MMSI,
        // skipping the chart's two-click quick-look since the list already
        // serves as the "first click".
        connect(aisListDlg_, &AisTargetListDialog::targetActivated,
                this, [this](quint32 mmsi) {
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
    }
    aisListDlg_->show();
    aisListDlg_->raise();
    aisListDlg_->activateWindow();
}

void MainWindow::showAisTarget(quint32 mmsi) {
    // Second click on the same target (its quick-look is still up) opens the
    // full info window.
    if (aisQuickInfo_ && aisQuickInfoMmsi_ == mmsi) {
        aisQuickInfo_->close();
        aisQuickInfoMmsi_ = 0;
        AisTargetInfoWindow* w = aisInfoWindows_.value(mmsi);
        if (!w) {
            w = new AisTargetInfoWindow(mmsi, aisStore_, this);
            w->setAttribute(Qt::WA_DeleteOnClose);
            aisInfoWindows_.insert(mmsi, w);
        }
        w->show();
        w->raise();
        w->activateWindow();
        return;
    }

    // First click (or a click on a different target): show the quick-look popup
    // near the cursor, replacing any popup already up for another target.
    if (aisQuickInfo_) aisQuickInfo_->close();
    auto* q = new AisQuickInfoWindow(mmsi, aisStore_, this);
    aisQuickInfo_     = q;
    aisQuickInfoMmsi_ = mmsi;
    q->move(QCursor::pos() + QPoint(14, 14));
    q->show();
}

void MainWindow::publishOwnshipToView() {
    if (view_) view_->setOwnship(navStore_->ownship());
}

void MainWindow::startScan(const QString& dir) {
    if (catalog_->isScanning()) return;
    root_ = dir;
    encScanDone_ = false;
    encScanOk_ = false;
    encScanMsg_.clear();
    rasterCount_ = 0;
    statusLeft_->setText(dir + QStringLiteral("   —   scanning…"));
    statusMid_->clear();
    catalog_->startScan(dir);
    // The raster layer scans the same folder for *.mbtiles, in parallel.
    view_->setRasterChartFolder(dir);
}

void MainWindow::onScanProgress(int done, int total) {
    statusLeft_->setText(root_ + QStringLiteral("   —   cataloging %1 / %2").arg(done).arg(total));
}

void MainWindow::onScanFinished(bool ok, const QString& message) {
    encScanDone_ = true;
    encScanOk_ = ok;
    encScanMsg_ = message;
    refreshChartStatus();
}

void MainWindow::onRasterChartsChanged(int count) {
    rasterCount_ = count;
    refreshChartStatus();
}

// Compose the status line from the two independent discovery results. A folder
// may hold ENC cells, raster (MBTiles) charts, both, or neither — so a folder
// with only raster charts is not treated as an error.
void MainWindow::refreshChartStatus() {
    QString s = root_ + QStringLiteral("   —   ");
    if (encScanOk_)              s += encScanMsg_;
    else if (rasterCount_ > 0)   s += QStringLiteral("no ENC cells");
    else if (encScanDone_)       s += QStringLiteral("no charts found");
    else                         s += QStringLiteral("scanning…");
    if (rasterCount_ > 0)
        s += QStringLiteral("   +   %1 raster chart(s)").arg(rasterCount_);
    statusLeft_->setText(s);
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
