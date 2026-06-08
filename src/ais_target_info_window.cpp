#include "ais_target_info_window.hpp"
#include "ais_target_store.hpp"

#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QDateTime>
#include <QLabel>
#include <vector>
#include <utility>

namespace {
QString fmtAngle(double v) { return QString::number(v, 'f', 1) + QStringLiteral("°"); }
QString fmtCoord(double v) { return QString::number(v, 'f', 6) + QStringLiteral("°"); }
QString fmtKnots(double v) { return QString::number(v, 'f', 1) + QStringLiteral(" kn"); }
QString fmtMeters(double v) { return QString::number(v, 'f', 1) + QStringLiteral(" m"); }

QString fmtFreshness(AisFreshness f, bool lost) {
    if (lost) return QStringLiteral("Lost");
    switch (f) {
        case AisFreshness::Current: return QStringLiteral("Current");
        case AisFreshness::Stale:   return QStringLiteral("Stale");
        case AisFreshness::Lost:    return QStringLiteral("Lost");
    }
    return QString();
}

QString fmtClass(AisClass c) {
    switch (c) {
        case AisClass::A: return QStringLiteral("A");
        case AisClass::B: return QStringLiteral("B");
        case AisClass::Unknown: break;
    }
    return QStringLiteral("?");
}
} // namespace

AisTargetInfoWindow::AisTargetInfoWindow(quint32 mmsi, const AisTargetStore* store,
                                         QWidget* parent)
    : QDialog(parent), mmsi_(mmsi), store_(store) {
    setWindowTitle(QStringLiteral("AIS Target — MMSI %1").arg(mmsi));
    resize(420, 540);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    col->addWidget(table_);

    if (store_) {
        connect(store_, &AisTargetStore::targetUpdated, this, &AisTargetInfoWindow::onTargetUpdated);
        connect(store_, &AisTargetStore::targetExpired, this, &AisTargetInfoWindow::onTargetExpired);
    }
    // Keep the Age column counting even when no fresh report arrives.
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &AisTargetInfoWindow::refresh);
    timer_->start();

    refresh();
}

void AisTargetInfoWindow::onTargetUpdated(quint32 mmsi) {
    if (mmsi == mmsi_) { lost_ = false; refresh(); }
}

void AisTargetInfoWindow::onTargetExpired(quint32 mmsi) {
    if (mmsi == mmsi_) { lost_ = true; refresh(); }
}

void AisTargetInfoWindow::setRow(int row, const QString& name, const QString& value) {
    auto ensure = [this](int r, int c) {
        if (auto* it = table_->item(r, c)) return it;
        auto* it = new QTableWidgetItem;
        table_->setItem(r, c, it);
        return it;
    };
    ensure(row, 0)->setText(name);
    ensure(row, 1)->setText(value);
}

void AisTargetInfoWindow::refresh() {
    // If the target is still tracked, read its current state; otherwise we keep
    // showing the last known fields (Status: Lost).
    static const AisTarget kEmpty;
    const AisTarget* live = store_ ? store_->target(mmsi_) : nullptr;
    const AisTarget& t = live ? *live : kEmpty;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    std::vector<std::pair<QString, QString>> rows;
    rows.emplace_back(QStringLiteral("MMSI"), QString::number(mmsi_));
    if (t.cls != AisClass::Unknown)
        rows.emplace_back(QStringLiteral("Class"), fmtClass(t.cls));

    if (!t.name.isEmpty())        rows.emplace_back(QStringLiteral("Name"),        t.name);
    if (!t.callSign.isEmpty())    rows.emplace_back(QStringLiteral("Call sign"),   t.callSign);
    if (t.shipType)               rows.emplace_back(QStringLiteral("Ship type"),   aisShipTypeName(*t.shipType));
    if (t.imoNumber)              rows.emplace_back(QStringLiteral("IMO"),         QString::number(*t.imoNumber));
    if (!t.destination.isEmpty()) rows.emplace_back(QStringLiteral("Destination"), t.destination);
    if (t.draughtMeters)          rows.emplace_back(QStringLiteral("Draught"),     fmtMeters(*t.draughtMeters));

    if (t.dimensions.known()) {
        rows.emplace_back(QStringLiteral("Length"),
                          fmtMeters(t.dimensions.lengthMeters()));
        rows.emplace_back(QStringLiteral("Beam"),
                          fmtMeters(t.dimensions.beamMeters()));
    }

    if (t.latitudeDeg)    rows.emplace_back(QStringLiteral("Latitude"),  fmtCoord(*t.latitudeDeg));
    if (t.longitudeDeg)   rows.emplace_back(QStringLiteral("Longitude"), fmtCoord(*t.longitudeDeg));
    if (t.cogDegTrue)     rows.emplace_back(QStringLiteral("COG (true)"), fmtAngle(*t.cogDegTrue));
    if (t.sogKnots)       rows.emplace_back(QStringLiteral("SOG"),       fmtKnots(*t.sogKnots));
    if (t.headingDegTrue) rows.emplace_back(QStringLiteral("Heading"),   fmtAngle(*t.headingDegTrue));
    if (t.rotDegPerMin)
        rows.emplace_back(QStringLiteral("Rate of turn"),
                          QString::number(*t.rotDegPerMin, 'f', 1) + QStringLiteral(" °/min"));

    if (t.navStatus != AisNavStatus::Undefined && t.cls == AisClass::A)
        rows.emplace_back(QStringLiteral("Nav status"), aisNavStatusName(int(t.navStatus)));

    if (t.cpaMeters)
        rows.emplace_back(QStringLiteral("CPA"),
                          QString::number(*t.cpaMeters / 1852.0, 'f', 2) + QStringLiteral(" nm"));
    if (t.tcpaSeconds)
        rows.emplace_back(QStringLiteral("TCPA"),
                          QString::number(*t.tcpaSeconds / 60.0, 'f', 1) + QStringLiteral(" min"));

    if (!t.source.isEmpty()) rows.emplace_back(QStringLiteral("Source"), t.source);

    // Age (relative to last update) and current freshness. When the target is
    // gone from the store we use the empty target's defaults; show "Lost" so
    // the user knows the window is showing a snapshot of the last known data.
    const double age = (live && t.lastUpdateUtc.isValid())
        ? t.lastUpdateUtc.msecsTo(now) / 1000.0
        : (live ? 0.0 : -1.0);
    if (age >= 0.0)
        rows.emplace_back(QStringLiteral("Age"),
                          QString::number(age, 'f', 0) + QStringLiteral(" s"));
    rows.emplace_back(QStringLiteral("Status"),
                      fmtFreshness(live ? t.freshness : AisFreshness::Lost, lost_ || !live));

    if (table_->rowCount() != int(rows.size()))
        table_->setRowCount(int(rows.size()));
    for (int i = 0; i < int(rows.size()); ++i)
        setRow(i, rows[i].first, rows[i].second);
}
