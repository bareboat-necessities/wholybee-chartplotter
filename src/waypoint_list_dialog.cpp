#include "waypoint_list_dialog.hpp"
#include "route_store.hpp"
#include "units.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>

namespace {
QLabel* makeCell(int fixedWidth, Qt::Alignment align) {
    auto* l = new QLabel;
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setAlignment(align);
    l->setStyleSheet(QStringLiteral("font-size:14px; padding:0 4px; border:none;"));
    if (fixedWidth > 0) l->setFixedWidth(fixedWidth);
    return l;
}

QString fmtPos(double lat, double lon) {
    return units::formatLatitude(lat) + QStringLiteral("  ")
         + units::formatLongitude(lon);
}
}  // namespace

WaypointListDialog::WaypointListDialog(RouteStore* store, bool pickMode, QWidget* parent)
    : QDialog(parent), store_(store), pickMode_(pickMode) {
    setWindowTitle(pickMode ? QStringLiteral("Select Waypoint")
                            : QStringLiteral("Waypoints"));
    resize(520, 600);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(0);
    col->setContentsMargins(0, 0, 0, 8);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("font-size:13px; padding:6px 8px;"));
    col->addWidget(countLabel_);

    {   // static column header
        auto* hdr = new QWidget(this);
        hdr->setStyleSheet(QStringLiteral(
            "background: palette(button); font-size:12px; font-weight:600;"));
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(8, 4, 8, 4);
        hl->setSpacing(0);
        auto* hName = new QLabel(QStringLiteral("Name"), hdr);
        hName->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* hPos = new QLabel(QStringLiteral("Position"), hdr);
        hPos->setFixedWidth(250);
        hPos->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* hVis = new QLabel(QStringLiteral("Visible"), hdr);
        hVis->setFixedWidth(64);
        hVis->setAlignment(Qt::AlignCenter);
        hl->addWidget(hName);
        hl->addWidget(hPos);
        hl->addWidget(hVis);
        col->addWidget(hdr);
    }

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(true);
    rowContainer_ = new QWidget;
    rowLayout_ = new QVBoxLayout(rowContainer_);
    rowLayout_->setContentsMargins(0, 0, 0, 0);
    rowLayout_->setSpacing(0);
    rowLayout_->addStretch(1);
    scrollArea_->setWidget(rowContainer_);
    QScroller::grabGesture(scrollArea_->viewport(), QScroller::LeftMouseButtonGesture);
    col->addWidget(scrollArea_, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(8, 8, 8, 0);
    deleteBtn_ = new QPushButton(QStringLiteral("Delete"));
    deleteBtn_->setMinimumHeight(44);
    deleteBtn_->setEnabled(false);
    deleteBtn_->setVisible(!pickMode_);
    connect(deleteBtn_, &QPushButton::clicked, this, &WaypointListDialog::deleteSelected);
    btnRow->addWidget(deleteBtn_);
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton(pickMode_ ? QStringLiteral("Cancel")
                                               : QStringLiteral("Close"));
    closeBtn->setMinimumHeight(44);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(closeBtn);
    col->addLayout(btnRow);

    if (store_) connect(store_, &RouteStore::waypointsChanged, this, &WaypointListDialog::refresh);
    refresh();
}

WaypointListDialog::Row WaypointListDialog::makeRow() {
    Row r;
    r.btn = new QPushButton(rowContainer_);
    r.btn->setFlat(true);
    r.btn->setMinimumHeight(44);
    r.btn->setCheckable(true);
    r.btn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align:left; border:none;"
        " border-bottom:1px solid palette(mid); }"
        "QPushButton:checked { background:palette(highlight);"
        " color:palette(highlighted-text); }"));
    auto* hl = new QHBoxLayout(r.btn);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(0);
    r.name = makeCell(0,   Qt::AlignLeft  | Qt::AlignVCenter);
    r.name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    r.pos  = makeCell(250, Qt::AlignRight | Qt::AlignVCenter);
    hl->addWidget(r.name, 1);
    hl->addWidget(r.pos);

    r.vis = new QCheckBox(r.btn);
    r.vis->setFixedWidth(64);
    r.vis->setStyleSheet(QStringLiteral("margin-left:24px;"));
    hl->addWidget(r.vis);

    connect(r.btn, &QPushButton::clicked, this, [this, btn = r.btn] {
        const qint64 id = btn->property("wid").toLongLong();
        if (pickMode_) { emit waypointPicked(id); accept(); }
        else           { selectRow(id); }
    });
    connect(r.vis, &QCheckBox::toggled, this, [this, box = r.vis](bool on) {
        const qint64 id = box->property("wid").toLongLong();
        if (store_) store_->setWaypointVisible(id, on);
    });
    return r;
}

void WaypointListDialog::refresh() {
    if (!store_) return;
    const QVector<Waypoint>& wpts = store_->waypoints();

    countLabel_->setText(QStringLiteral("%1 waypoint%2")
                         .arg(wpts.size()).arg(wpts.size() == 1 ? "" : "s"));

    const int want = wpts.size();
    while (int(rows_.size()) < want) {
        Row r = makeRow();
        rowLayout_->insertWidget(rowLayout_->count() - 1, r.btn);
        rows_.push_back(r);
    }
    while (int(rows_.size()) > want) {
        rows_.back().btn->deleteLater();
        rows_.pop_back();
    }

    bool selectionStillExists = false;
    for (int i = 0; i < want; ++i) {
        const Waypoint& w = wpts[i];
        Row& row = rows_[i];
        row.id = w.id;
        row.btn->setProperty("wid", QVariant::fromValue(w.id));
        row.vis->setProperty("wid", QVariant::fromValue(w.id));
        const QString nm = w.name.isEmpty() ? QStringLiteral("(unnamed)") : w.name;
        if (row.name->text() != nm) row.name->setText(nm);
        row.pos->setText(fmtPos(w.lat, w.lon));
        row.vis->blockSignals(true);
        row.vis->setChecked(w.visible);
        row.vis->blockSignals(false);
        if (w.id == selectedId_) selectionStillExists = true;
    }
    if (!selectionStillExists) selectedId_ = -1;
    restyleRows();
    deleteBtn_->setEnabled(selectedId_ >= 0);
}

void WaypointListDialog::selectRow(qint64 id) {
    selectedId_ = (selectedId_ == id) ? -1 : id;
    restyleRows();
    deleteBtn_->setEnabled(selectedId_ >= 0);
}

void WaypointListDialog::restyleRows() {
    for (Row& r : rows_)
        r.btn->setChecked(r.id == selectedId_ && selectedId_ >= 0);
}

void WaypointListDialog::deleteSelected() {
    if (!store_ || selectedId_ < 0) return;
    QString nm = QStringLiteral("this waypoint");
    for (const Waypoint& w : store_->waypoints())
        if (w.id == selectedId_) { if (!w.name.isEmpty()) nm = w.name; break; }
    if (QMessageBox::question(this, QStringLiteral("Delete Waypoint"),
            QStringLiteral("Delete %1?").arg(nm)) != QMessageBox::Yes)
        return;
    const qint64 id = selectedId_;
    selectedId_ = -1;
    store_->removeWaypoint(id);
}
