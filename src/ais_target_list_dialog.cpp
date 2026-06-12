#include "ais_target_list_dialog.hpp"
#include "ais_target_store.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QScroller>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <algorithm>
#include <limits>
#include <vector>

namespace {
constexpr double kMetresPerNm = 1852.0;
const QString kUnknown   = QStringLiteral("unknown");
const QString kNoNumber  = QStringLiteral("—");

QString fmtDistance(const std::optional<double>& rangeMeters) {
    if (!rangeMeters) return kNoNumber;
    return QString::number(*rangeMeters / kMetresPerNm, 'f', 2) + QStringLiteral(" nm");
}
} // namespace

AisTargetListDialog::AisTargetListDialog(const AisTargetStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("AIS Targets"));
    resize(560, 600);
    setWindowFlag(Qt::Window, true);   // modeless top-level window

    auto* col = new QVBoxLayout(this);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("font-size:13px; padding:2px 4px;"));
    col->addWidget(countLabel_);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("MMSI"),
         QStringLiteral("Call sign"), QStringLiteral("Distance")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Single-row selection gives a brief visual cue on tap; the click handler
    // then opens the full info window for that target.
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    // Touch-comfortable row height (matches the menu's 56px tap target idiom,
    // a touch shorter so more rows fit on a tablet screen).
    table_->verticalHeader()->setDefaultSectionSize(44);
    QHeaderView* hh = table_->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::Stretch);            // Name
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);   // MMSI
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents);   // Call sign
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents);   // Distance
    hh->setSectionsClickable(false);
    // Touch: drag anywhere on the table body to scroll. Hide the scrollbar so
    // it doesn't compete with the kinetic gesture for stray touches.
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(table_->viewport(), QScroller::LeftMouseButtonGesture);
    col->addWidget(table_, 1);

    // Tapping a row opens the full info window for that target. We pull the
    // MMSI from the row's column-0 user-data so the dispatch is immune to the
    // current sort order (refresh() rewrites it).
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (auto* it = table_->item(row, 0)) {
            const quint32 mmsi = it->data(Qt::UserRole).toUInt();
            if (mmsi) emit targetActivated(mmsi);
        }
    });

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setMinimumHeight(44);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(closeBtn);
    col->addLayout(row);

    if (store_) {
        connect(store_, &AisTargetStore::targetUpdated, this, &AisTargetListDialog::refresh);
        connect(store_, &AisTargetStore::targetExpired, this, &AisTargetListDialog::refresh);
    }
    // Distance ticks along with ownship/target motion even when the per-target
    // signals are quiet (e.g. no fresh AIS reports but ownship has moved).
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &AisTargetListDialog::refresh);
    timer_->start();

    refresh();
}

void AisTargetListDialog::refresh() {
    if (!store_) return;

    // Snapshot the targets, then sort by distance (ascending; absent ranges sink
    // to the bottom so usable contacts stay on screen).
    std::vector<const AisTarget*> rows;
    rows.reserve(store_->targets().size());
    for (const AisTarget& t : store_->targets()) rows.push_back(&t);
    std::sort(rows.begin(), rows.end(), [](const AisTarget* a, const AisTarget* b) {
        const double da = a->rangeMeters.value_or(std::numeric_limits<double>::infinity());
        const double db = b->rangeMeters.value_or(std::numeric_limits<double>::infinity());
        if (da != db) return da < db;
        return a->mmsi < b->mmsi;        // stable tiebreak
    });

    countLabel_->setText(QStringLiteral("Tracking %1 target%2")
                         .arg(rows.size()).arg(rows.size() == 1 ? "" : "s"));

    if (table_->rowCount() != int(rows.size()))
        table_->setRowCount(int(rows.size()));

    auto setCell = [this](int r, int c, const QString& text, Qt::Alignment align) {
        QTableWidgetItem* it = table_->item(r, c);
        if (!it) { it = new QTableWidgetItem; table_->setItem(r, c, it); }
        if (it->text() != text) it->setText(text);
        it->setTextAlignment(align);
    };

    for (int i = 0; i < int(rows.size()); ++i) {
        const AisTarget* t = rows[i];
        setCell(i, 0, t->name.isEmpty()     ? kUnknown : t->name,
                Qt::AlignLeft  | Qt::AlignVCenter);
        // Stash the MMSI on column 0 so the click handler can recover it
        // independent of the row's current display position.
        table_->item(i, 0)->setData(Qt::UserRole, t->mmsi);
        setCell(i, 1, QString::number(t->mmsi),
                Qt::AlignRight | Qt::AlignVCenter);
        setCell(i, 2, t->callSign.isEmpty() ? kUnknown : t->callSign,
                Qt::AlignLeft  | Qt::AlignVCenter);
        setCell(i, 3, fmtDistance(t->rangeMeters),
                Qt::AlignRight | Qt::AlignVCenter);
    }
}
