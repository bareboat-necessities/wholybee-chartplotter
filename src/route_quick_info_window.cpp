#include "route_quick_info_window.hpp"
#include "route_store.hpp"
#include "theme.hpp"
#include "units.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

RouteQuickInfoWindow::RouteQuickInfoWindow(ClickedRouteObject::Kind kind, qint64 id,
                                           const RouteStore* store, QWidget* parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint),
      kind_(kind), id_(id), store_(store) {
    // Show without grabbing focus so chart pans still dismiss this popup, and
    // delete on close so the host's QPointer clears.
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "RouteQuickInfoWindow { background:%1; border:1px solid %2; border-radius:6px; }"
        "QPushButton { min-height:34px; padding:0 12px; }")
        .arg(theme::menu().panelBg, theme::menu().panelBorder));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 8, 12, 10);
    col->setSpacing(4);

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral("font-size:15px; font-weight:600;"));
    col->addWidget(titleLabel_);

    subLabel_ = new QLabel(this);
    subLabel_->setStyleSheet(QStringLiteral("font-size:13px;"));
    col->addWidget(subLabel_);

    // Action buttons. Two rows of two so the popup stays narrow on a tablet.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    grid->setContentsMargins(0, 6, 0, 0);

    auto* renameBtn = new QPushButton(QStringLiteral("Rename"), this);
    auto* editBtn   = new QPushButton(
        kind_ == ClickedRouteObject::Kind::Route
            ? QStringLiteral("Edit on chart")
            : QStringLiteral("Drag on chart"), this);
    auto* propsBtn  = new QPushButton(QStringLiteral("Properties…"), this);
    visBtn_         = new QPushButton(QStringLiteral("Hide"), this);
    auto* delBtn    = new QPushButton(QStringLiteral("Delete"), this);

    grid->addWidget(renameBtn, 0, 0);
    grid->addWidget(editBtn,   0, 1);
    grid->addWidget(propsBtn,  1, 0);
    grid->addWidget(visBtn_,   1, 1);
    grid->addWidget(delBtn,    2, 0, 1, 2);
    col->addLayout(grid);

    connect(renameBtn, &QPushButton::clicked, this, [this] { emit renameRequested(); close(); });
    connect(editBtn,   &QPushButton::clicked, this, [this] { emit editRequested(); close(); });
    connect(propsBtn,  &QPushButton::clicked, this, [this] { emit propertiesRequested(); close(); });
    connect(visBtn_,   &QPushButton::clicked, this, [this] {
        emit visibilityToggleRequested();
        close();
    });
    connect(delBtn,    &QPushButton::clicked, this, [this] { emit deleteRequested(); close(); });

    if (store_) {
        connect(store_, &RouteStore::routesChanged,    this, &RouteQuickInfoWindow::refresh);
        connect(store_, &RouteStore::waypointsChanged, this, &RouteQuickInfoWindow::refresh);
    }
    refresh();
}

void RouteQuickInfoWindow::refresh() {
    if (!store_) return;
    if (kind_ == ClickedRouteObject::Kind::Waypoint) {
        const Waypoint* w = nullptr;
        for (const Waypoint& cand : store_->waypoints())
            if (cand.id == id_) { w = &cand; break; }
        if (!w) { close(); return; }   // deleted out from under us
        titleLabel_->setText(w->name.isEmpty() ? QStringLiteral("(unnamed waypoint)") : w->name);
        subLabel_->setText(units::formatLatitude(w->lat) + QStringLiteral("  ")
                           + units::formatLongitude(w->lon));
        visBtn_->setText(w->visible ? QStringLiteral("Hide") : QStringLiteral("Show"));
    } else {
        const Route* r = store_->route(id_);
        if (!r) { close(); return; }
        titleLabel_->setText(r->name.isEmpty() ? QStringLiteral("(unnamed route)") : r->name);
        subLabel_->setText(QStringLiteral("%1 point%2")
                            .arg(r->points.size())
                            .arg(r->points.size() == 1 ? "" : "s"));
        visBtn_->setText(r->visible ? QStringLiteral("Hide") : QStringLiteral("Show"));
    }
    adjustSize();
}
