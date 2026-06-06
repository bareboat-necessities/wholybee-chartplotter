#include "side_menu.hpp"
#include "settings.hpp"

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
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

    auto* col = new QVBoxLayout(panel_);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("Menu"), panel_);
    title->setStyleSheet(QStringLiteral(
        "font-size:18px; font-weight:600; padding:18px 20px;"
        "background:#1a3a5c; color:white;"));
    col->addWidget(title);

    col->addWidget(makeHeader(QStringLiteral("Chart Sets")));
    auto* selectBtn = makeAction(QStringLiteral("Select Folder…"));
    connect(selectBtn, &QPushButton::clicked, this, [this] {
        emit selectFolderRequested();
        closeMenu();
    });
    col->addWidget(selectBtn);
    auto* rescanBtn = makeAction(QStringLiteral("Rescan Charts"));
    connect(rescanBtn, &QPushButton::clicked, this, [this] {
        emit rescanRequested();
        closeMenu();
    });
    col->addWidget(rescanBtn);

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

    auto* closeBtn = makeAction(QStringLiteral("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &SideMenu::closeMenu);
    col->addWidget(closeBtn);

    anim_ = new QPropertyAnimation(panel_, "geometry", this);
    anim_->setDuration(160);
    connect(anim_, &QPropertyAnimation::finished, this, [this] {
        if (!open_) setVisible(false);   // hide overlay once the slide-out ends
    });

    if (parent) parent->installEventFilter(this);
}

QLabel* SideMenu::makeHeader(const QString& text) {
    auto* h = new QLabel(text, panel_);
    h->setStyleSheet(QStringLiteral(
        "font-size:12px; font-weight:600; color:#5a5a5a;"
        "padding:14px 20px 6px 20px; background:#eef1f4;"));
    return h;
}

QPushButton* SideMenu::makeAction(const QString& text) {
    auto* b = new QPushButton(text, panel_);
    b->setMinimumHeight(56);   // comfortable touch target
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{ text-align:left; padding-left:24px; border:none;"
        " font-size:16px; background:#fbfbfb; }"
        "QPushButton:pressed{ background:#dce6f0; }"));
    return b;
}

QPushButton* SideMenu::makeToggle(const QString& text, bool checked) {
    auto* b = new QPushButton(panel_);
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

void SideMenu::openMenu() {
    if (open_) return;
    open_ = true;
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
