#include "side_menu.hpp"
#include "settings.hpp"

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QPropertyAnimation>
#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>

SideMenu::SideMenu(Settings* settings, QWidget* parent)
    : QWidget(parent), settings_(settings) {
    setVisible(false);

    // Scrim covers the whole chart; a tap anywhere on it closes the menu.
    scrim_ = new QWidget(this);
    scrim_->setStyleSheet(QStringLiteral("background: rgba(0,0,0,90);"));
    scrim_->installEventFilter(this);

    // Panel: the opaque menu surface that slides in from the left edge.
    panel_ = new QWidget(this);
    panel_->setStyleSheet(QStringLiteral(
        "background:#fbfbfb; border-right:1px solid #c8c8c8;"));

    auto* outer = new QVBoxLayout(panel_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    title_ = new QLabel(QStringLiteral("Menu"), panel_);
    title_->setStyleSheet(QStringLiteral(
        "font-size:18px; font-weight:600; padding:18px 20px;"
        "background:#1a3a5c; color:white;"));
    outer->addWidget(title_);

    stack_ = new QStackedWidget(panel_);
    mainIndex_     = stack_->addWidget(wrapScroll(buildMainPage()));
    settingsIndex_ = stack_->addWidget(wrapScroll(buildSettingsPage()));
    outer->addWidget(stack_, 1);

    anim_ = new QPropertyAnimation(panel_, "geometry", this);
    anim_->setDuration(160);
    connect(anim_, &QPropertyAnimation::finished, this, [this] {
        if (!open_) setVisible(false);   // hide overlay once the slide-out ends
    });

    // Keep the chart-set list (and its active highlight) current.
    connect(settings_, &Settings::chartSetsChanged, this, &SideMenu::rebuildChartSets);
    connect(settings_, &Settings::chartDirectoryChanged,
            this, [this](const QString&) { rebuildChartSets(); });

    if (parent) parent->installEventFilter(this);
}

// ---- page construction ----------------------------------------------------

QWidget* SideMenu::buildMainPage() {
    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    col->addWidget(makeHeader(QStringLiteral("Chart Sets")));
    auto* setsHolder = new QWidget(page);
    chartSetsBox_ = new QVBoxLayout(setsHolder);
    chartSetsBox_->setContentsMargins(0, 0, 0, 0);
    chartSetsBox_->setSpacing(0);
    col->addWidget(setsHolder);
    rebuildChartSets();

    col->addWidget(makeHeader(QStringLiteral("View")));
    auto* fitBtn = makeAction(QStringLiteral("Fit to Charts"));
    connect(fitBtn, &QPushButton::clicked, this, [this] {
        emit fitRequested();
        closeMenu();
    });
    col->addWidget(fitBtn);

    auto* snd = makeToggle(QStringLiteral("Soundings"), settings_->showSoundings());
    connect(snd, &QPushButton::toggled, settings_, &Settings::setShowSoundings);
    col->addWidget(snd);
    auto* sym = makeToggle(QStringLiteral("Symbols"), settings_->showSymbols());
    connect(sym, &QPushButton::toggled, settings_, &Settings::setShowSymbols);
    col->addWidget(sym);
    auto* con = makeToggle(QStringLiteral("Depth Contours"), settings_->showDepthContours());
    connect(con, &QPushButton::toggled, settings_, &Settings::setShowDepthContours);
    col->addWidget(con);

    col->addStretch(1);

    auto* settingsBtn = makeAction(QStringLiteral("Settings"));
    connect(settingsBtn, &QPushButton::clicked, this, &SideMenu::showSettingsPage);
    col->addWidget(settingsBtn);
    auto* closeBtn = makeAction(QStringLiteral("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &SideMenu::closeMenu);
    col->addWidget(closeBtn);

    return page;
}

QWidget* SideMenu::buildSettingsPage() {
    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    col->addWidget(makeHeader(QStringLiteral("Charts")));
    auto* chartSetsBtn = makeAction(QStringLiteral("Chart Sets"));
    connect(chartSetsBtn, &QPushButton::clicked, this,
            [this] { emit manageChartSetsRequested(); });
    col->addWidget(chartSetsBtn);

    auto* basemapBtn = makeAction(QStringLiteral("Basemap Folder…"));
    connect(basemapBtn, &QPushButton::clicked, this,
            [this] { emit basemapFolderRequested(); });
    col->addWidget(basemapBtn);

    auto* unitsBtn = makeAction(QStringLiteral("Units…"));
    connect(unitsBtn, &QPushButton::clicked, this,
            [this] { emit editUnitsRequested(); });
    col->addWidget(unitsBtn);

    col->addWidget(makeHeader(QStringLiteral("Data Connections")));
    auto* sim = makeToggle(QStringLiteral("Simulator"), settings_->simulatorEnabled());
    connect(sim, &QPushButton::toggled, settings_, &Settings::setSimulatorEnabled);
    // Keep the toggle in sync if the setting changes elsewhere.
    connect(settings_, &Settings::simulatorEnabledChanged, sim, [sim](bool on) {
        if (sim->isChecked() != on) sim->setChecked(on);
    });
    col->addWidget(sim);
    auto* staleBtn = makeAction(QStringLiteral("Stale Data Thresholds…"));
    connect(staleBtn, &QPushButton::clicked, this,
            [this] { emit editStaleThresholdsRequested(); });
    col->addWidget(staleBtn);

    col->addWidget(makeHeader(QStringLiteral("Ships")));
    auto* predBtn = makeAction(QStringLiteral("Ownship Course Prediction…"));
    connect(predBtn, &QPushButton::clicked, this,
            [this] { emit editOwnshipPredictionRequested(); });
    col->addWidget(predBtn);

    col->addStretch(1);

    auto* backBtn = makeAction(QStringLiteral("Back"));
    connect(backBtn, &QPushButton::clicked, this, &SideMenu::showMainPage);
    col->addWidget(backBtn);

    return page;
}

QWidget* SideMenu::wrapScroll(QWidget* content) {
    // Let a page scroll when its items are taller than the panel, so the content
    // is always sized by its items (never squeezed by the layout) and the list
    // can grow as more chart sets are added.
    auto* area = new QScrollArea;
    area->setWidget(content);
    area->setWidgetResizable(true);      // size content to items; scroll if taller
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget"
                                       "{ background:#fbfbfb; }"));
    // Touch: drag anywhere to scroll (kinetic). Taps still hit the buttons.
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
    return area;
}

void SideMenu::rebuildChartSets() {
    if (!chartSetsBox_) return;

    // Drop the existing set buttons before rebuilding from current settings.
    while (QLayoutItem* item = chartSetsBox_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    const QVector<ChartSet>& sets = settings_->chartSets();
    if (sets.isEmpty()) {
        auto* empty = new QLabel(
            QStringLiteral("No chart sets yet.\nAdd one in Settings → Chart Sets."));
        empty->setWordWrap(true);
        empty->setStyleSheet(QStringLiteral("color:#888; padding:14px 24px; font-size:14px;"));
        chartSetsBox_->addWidget(empty);
        return;
    }

    const QString active = settings_->chartDirectory();
    for (const ChartSet& cs : sets) {
        const bool isActive = (cs.directory == active);
        auto* b = makeAction((isActive ? QStringLiteral("✓  ")    // check mark
                                        : QStringLiteral("     ")) + cs.name);
        b->setToolTip(cs.directory);
        if (isActive)
            b->setStyleSheet(b->styleSheet() +
                QStringLiteral("QPushButton{ color:#12407a; font-weight:600; }"));
        const QString dir = cs.directory;
        connect(b, &QPushButton::clicked, this, [this, dir] {
            emit chartSetSelected(dir);
            closeMenu();
        });
        chartSetsBox_->addWidget(b);
    }
}

void SideMenu::showMainPage() {
    stack_->setCurrentIndex(mainIndex_);
    title_->setText(QStringLiteral("Menu"));
}

void SideMenu::showSettingsPage() {
    stack_->setCurrentIndex(settingsIndex_);
    title_->setText(QStringLiteral("Settings"));
}

// ---- widget factories -----------------------------------------------------

QLabel* SideMenu::makeHeader(const QString& text) {
    auto* h = new QLabel(text);
    h->setStyleSheet(QStringLiteral(
        "font-size:12px; font-weight:600; color:#5a5a5a;"
        "padding:14px 20px 6px 20px; background:#eef1f4;"));
    return h;
}

QPushButton* SideMenu::makeAction(const QString& text) {
    auto* b = new QPushButton(text);
    b->setMinimumHeight(56);   // comfortable touch target
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{ text-align:left; padding-left:24px; border:none;"
        " font-size:16px; background:#fbfbfb; }"
        "QPushButton:pressed{ background:#dce6f0; }"));
    return b;
}

QPushButton* SideMenu::makeToggle(const QString& text, bool checked) {
    auto* b = new QPushButton();
    b->setCheckable(true);
    b->setMinimumHeight(56);
    b->setCursor(Qt::PointingHandCursor);
    // A filled/hollow bullet shows state clearly without relying on the
    // platform checkbox indicator, which is too small for touch.
    auto sync = [b, text](bool on) {
        b->setText((on ? QStringLiteral("●   ") : QStringLiteral("○   ")) + text);
    };
    sync(checked);
    b->setChecked(checked);
    connect(b, &QPushButton::toggled, b, sync);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{ text-align:left; padding-left:24px; border:none;"
        " font-size:16px; background:#fbfbfb; }"
        "QPushButton:checked{ color:#12407a; }"
        "QPushButton:pressed{ background:#dce6f0; }"));
    return b;
}

// ---- open/close + geometry ------------------------------------------------

void SideMenu::openMenu() {
    if (open_) return;
    open_ = true;
    showMainPage();   // always start on the main page
    if (parentWidget()) setGeometry(parentWidget()->rect());
    scrim_->setGeometry(rect());
    scrim_->lower();
    panel_->setGeometry(-panelWidth_, 0, panelWidth_, height());
    panel_->raise();
    setVisible(true);
    raise();
    anim_->stop();
    anim_->setStartValue(panel_->geometry());
    anim_->setEndValue(QRect(0, 0, panelWidth_, height()));
    anim_->start();
}

void SideMenu::closeMenu() {
    if (!open_) return;
    open_ = false;
    anim_->stop();
    anim_->setStartValue(panel_->geometry());
    anim_->setEndValue(QRect(-panelWidth_, 0, panelWidth_, height()));
    anim_->start();
}

void SideMenu::layoutPanel() {
    scrim_->setGeometry(rect());
    scrim_->lower();
    if (anim_->state() != QAbstractAnimation::Running) {
        panel_->setGeometry(open_ ? QRect(0, 0, panelWidth_, height())
                                  : QRect(-panelWidth_, 0, panelWidth_, height()));
    }
    panel_->raise();
}

void SideMenu::resizeEvent(QResizeEvent*) {
    layoutPanel();
}

bool SideMenu::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize) {
        if (isVisible() && parentWidget())
            setGeometry(parentWidget()->rect());   // triggers our resizeEvent
    } else if (obj == scrim_ && e->type() == QEvent::MouseButtonPress) {
        closeMenu();
        return true;
    }
    return QWidget::eventFilter(obj, e);
}
