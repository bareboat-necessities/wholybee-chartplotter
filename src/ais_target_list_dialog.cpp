#include "ais_target_list_dialog.hpp"
#include "ais_target_store.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QPushButton>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <vector>

namespace {
constexpr double kMetresPerNm = 1852.0;
const QString kUnknown  = QStringLiteral("unknown");
const QString kNoNumber = QStringLiteral("—");

QString fmtDistance(const std::optional<double>& rangeMeters) {
    if (!rangeMeters) return kNoNumber;
    return QString::number(*rangeMeters / kMetresPerNm, 'f', 2) + QStringLiteral(" nm");
}

// A cell label that passes mouse events through to its parent row button, so a
// tap anywhere on the row registers as a button click (no per-label handling).
QLabel* makeCell(int fixedWidth, Qt::Alignment align) {
    auto* l = new QLabel;
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setAlignment(align);
    l->setStyleSheet(QStringLiteral("font-size:14px; padding:0 4px; border:none;"));
    if (fixedWidth > 0) l->setFixedWidth(fixedWidth);
    return l;
}
} // namespace

AisTargetListDialog::AisTargetListDialog(const AisTargetStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("AIS Targets"));
    resize(560, 600);
    setWindowFlag(Qt::Window, true);   // modeless top-level window

    auto* col = new QVBoxLayout(this);
    col->setSpacing(0);
    col->setContentsMargins(0, 0, 0, 8);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("font-size:13px; padding:6px 8px;"));
    col->addWidget(countLabel_);

    // Static column header row — outside the scroll area so it doesn't scroll.
    {
        auto* hdr = new QWidget(this);
        hdr->setStyleSheet(QStringLiteral(
            "background: palette(button); font-size:12px; font-weight:600;"));
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(8, 4, 8, 4);
        hl->setSpacing(0);
        auto* hName = new QLabel(QStringLiteral("Name"), hdr);
        hName->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* hMmsi = new QLabel(QStringLiteral("MMSI"), hdr);
        hMmsi->setFixedWidth(100);
        hMmsi->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* hCall = new QLabel(QStringLiteral("Call sign"), hdr);
        hCall->setFixedWidth(100);
        auto* hDist = new QLabel(QStringLiteral("Distance"), hdr);
        hDist->setFixedWidth(90);
        hDist->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(hName);
        hl->addWidget(hMmsi);
        hl->addWidget(hCall);
        hl->addWidget(hDist);
        col->addWidget(hdr);
    }

    // Scrollable row area. Uses QScrollArea — the same widget the side menu uses
    // via wrapScroll() — so QScroller delivers identical pixel-level drag-to-scroll
    // behaviour: content moves exactly with the finger, no velocity amplification.
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(true);

    rowContainer_ = new QWidget;
    rowLayout_ = new QVBoxLayout(rowContainer_);
    rowLayout_->setContentsMargins(0, 0, 0, 0);
    rowLayout_->setSpacing(0);
    rowLayout_->addStretch(1);          // keeps rows packed to the top
    scrollArea_->setWidget(rowContainer_);

    QScroller::grabGesture(scrollArea_->viewport(), QScroller::LeftMouseButtonGesture);
    col->addWidget(scrollArea_, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(8, 8, 8, 0);
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setMinimumHeight(44);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    col->addLayout(btnRow);

    // Coalesce bursts of per-target signals (one per AIS message) into at most
    // one rebuild per 250 ms interval.
    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    coalesce_->setInterval(250);
    connect(coalesce_, &QTimer::timeout, this, &AisTargetListDialog::refresh);

    if (store_) {
        connect(store_, &AisTargetStore::targetUpdated, this, &AisTargetListDialog::scheduleRefresh);
        connect(store_, &AisTargetStore::targetExpired, this, &AisTargetListDialog::scheduleRefresh);
    }

    // Distance ticks along with ownship/target motion even when no new AIS
    // messages arrive (e.g. no reports but ownship is underway).
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &AisTargetListDialog::refresh);
    timer_->start();

    refresh();
}

void AisTargetListDialog::scheduleRefresh() {
    if (!coalesce_->isActive()) coalesce_->start();
}

AisTargetListDialog::Row AisTargetListDialog::makeRow() {
    Row r;
    // Flat button = the tap target. Qt handles click detection; child labels are
    // transparent to the mouse so a tap anywhere on the row hits the button.
    r.btn = new QPushButton(rowContainer_);
    r.btn->setFlat(true);
    r.btn->setMinimumHeight(44);
    r.btn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align:left; border:none;"
        " border-bottom:1px solid palette(mid); }"
        "QPushButton:pressed { background:palette(highlight);"
        " color:palette(highlighted-text); }"));

    auto* hl = new QHBoxLayout(r.btn);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(0);
    r.name = makeCell(0,   Qt::AlignLeft  | Qt::AlignVCenter);
    r.name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    r.mmsi = makeCell(100, Qt::AlignRight | Qt::AlignVCenter);
    r.call = makeCell(100, Qt::AlignLeft  | Qt::AlignVCenter);
    r.dist = makeCell(90,  Qt::AlignRight | Qt::AlignVCenter);
    hl->addWidget(r.name, 1);
    hl->addWidget(r.mmsi);
    hl->addWidget(r.call);
    hl->addWidget(r.dist);

    // The row's current MMSI lives in a dynamic property, refreshed each rebuild;
    // the click handler reads it so the dispatch follows the row's current target
    // regardless of sort order.
    connect(r.btn, &QPushButton::clicked, this, [this, btn = r.btn] {
        const quint32 mmsi = btn->property("mmsi").toUInt();
        if (mmsi) emit targetActivated(mmsi);
    });
    return r;
}

void AisTargetListDialog::refresh() {
    if (!store_) return;

    // Snapshot and sort by distance ascending; targets without a range sink to
    // the bottom so usable contacts stay visible.
    std::vector<const AisTarget*> targets;
    targets.reserve(store_->targets().size());
    for (const AisTarget& t : store_->targets()) targets.push_back(&t);
    std::sort(targets.begin(), targets.end(), [](const AisTarget* a, const AisTarget* b) {
        const double da = a->rangeMeters.value_or(std::numeric_limits<double>::infinity());
        const double db = b->rangeMeters.value_or(std::numeric_limits<double>::infinity());
        if (da != db) return da < db;
        return a->mmsi < b->mmsi;
    });

    countLabel_->setText(QStringLiteral("Tracking %1 target%2")
                         .arg(targets.size()).arg(targets.size() == 1 ? "" : "s"));

    // Grow / shrink the reusable row pool to match the target count. Insert new
    // rows before the trailing stretch (the last layout item); destroy surplus
    // rows from the tail. We never touch rows that stay, so the scroll position
    // and any in-progress drag are preserved and nothing flickers.
    const int want = int(targets.size());
    while (int(rows_.size()) < want) {
        Row r = makeRow();
        rowLayout_->insertWidget(rowLayout_->count() - 1, r.btn);
        rows_.push_back(r);
    }
    while (int(rows_.size()) > want) {
        rows_.back().btn->deleteLater();
        rows_.pop_back();
    }

    // Update the surviving rows in place.
    auto setText = [](QLabel* l, const QString& s) { if (l->text() != s) l->setText(s); };
    for (int i = 0; i < want; ++i) {
        const AisTarget* t = targets[i];
        const Row& r = rows_[i];
        setText(r.name, t->name.isEmpty()     ? kUnknown : t->name);
        setText(r.mmsi, QString::number(t->mmsi));
        setText(r.call, t->callSign.isEmpty() ? kUnknown : t->callSign);
        setText(r.dist, fmtDistance(t->rangeMeters));
        r.btn->setProperty("mmsi", t->mmsi);
    }
}
