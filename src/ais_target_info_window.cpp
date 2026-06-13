#include "ais_target_info_window.hpp"
#include "ais_target_store.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
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
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Scrollable field/value area. Uses QScrollArea so QScroller delivers the
    // same pixel-level drag-to-scroll behaviour as the side menu and target list.
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

AisTargetInfoWindow::Row AisTargetInfoWindow::makeRow() {
    Row r;
    r.widget = new QWidget(rowContainer_);
    r.widget->setStyleSheet(QStringLiteral(
        "QWidget { border-bottom:1px solid palette(mid); }"));
    auto* hl = new QHBoxLayout(r.widget);
    hl->setContentsMargins(12, 8, 12, 8);
    hl->setSpacing(8);

    r.field = new QLabel(r.widget);
    r.field->setFixedWidth(130);
    r.field->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600; border:none;"));
    r.field->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    r.value = new QLabel(r.widget);
    r.value->setStyleSheet(QStringLiteral("font-size:14px; border:none;"));
    r.value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    r.value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    r.value->setWordWrap(true);

    hl->addWidget(r.field);
    hl->addWidget(r.value, 1);
    return r;
}

void AisTargetInfoWindow::refresh() {
    // If the target is still tracked, read its current state; otherwise we keep
    // showing the last known fields (Status: Lost).
    static const AisTarget kEmpty;
    const AisTarget* live = store_ ? store_->target(mmsi_) : nullptr;
    const AisTarget& t = live ? *live : kEmpty;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    std::vector<std::pair<QString, QString>> fields;
    fields.emplace_back(QStringLiteral("MMSI"), QString::number(mmsi_));
    if (t.cls != AisClass::Unknown)
        fields.emplace_back(QStringLiteral("Class"), fmtClass(t.cls));

    if (!t.name.isEmpty())        fields.emplace_back(QStringLiteral("Name"),        t.name);
    if (!t.callSign.isEmpty())    fields.emplace_back(QStringLiteral("Call sign"),   t.callSign);
    if (t.shipType)               fields.emplace_back(QStringLiteral("Ship type"),   aisShipTypeName(*t.shipType));
    if (t.imoNumber)              fields.emplace_back(QStringLiteral("IMO"),         QString::number(*t.imoNumber));
    if (!t.destination.isEmpty()) fields.emplace_back(QStringLiteral("Destination"), t.destination);
    if (t.draughtMeters)          fields.emplace_back(QStringLiteral("Draught"),     fmtMeters(*t.draughtMeters));

    if (t.dimensions.known()) {
        fields.emplace_back(QStringLiteral("Length"),
                            fmtMeters(t.dimensions.lengthMeters()));
        fields.emplace_back(QStringLiteral("Beam"),
                            fmtMeters(t.dimensions.beamMeters()));
    }

    if (t.latitudeDeg)    fields.emplace_back(QStringLiteral("Latitude"),  fmtCoord(*t.latitudeDeg));
    if (t.longitudeDeg)   fields.emplace_back(QStringLiteral("Longitude"), fmtCoord(*t.longitudeDeg));
    if (t.cogDegTrue)     fields.emplace_back(QStringLiteral("COG (true)"), fmtAngle(*t.cogDegTrue));
    if (t.sogKnots)       fields.emplace_back(QStringLiteral("SOG"),       fmtKnots(*t.sogKnots));
    if (t.headingDegTrue) fields.emplace_back(QStringLiteral("Heading"),   fmtAngle(*t.headingDegTrue));
    if (t.rotDegPerMin)
        fields.emplace_back(QStringLiteral("Rate of turn"),
                            QString::number(*t.rotDegPerMin, 'f', 1) + QStringLiteral(" °/min"));

    if (t.navStatus != AisNavStatus::Undefined && t.cls == AisClass::A)
        fields.emplace_back(QStringLiteral("Nav status"), aisNavStatusName(int(t.navStatus)));

    if (t.rangeMeters)
        fields.emplace_back(QStringLiteral("Distance"),
                            QString::number(*t.rangeMeters / 1852.0, 'f', 2) + QStringLiteral(" nm"));
    if (t.cpaMeters)
        fields.emplace_back(QStringLiteral("CPA"),
                            QString::number(*t.cpaMeters / 1852.0, 'f', 2) + QStringLiteral(" nm"));
    if (t.tcpaSeconds)
        fields.emplace_back(QStringLiteral("TCPA"), aisFormatTcpa(*t.tcpaSeconds));

    if (!t.source.isEmpty()) fields.emplace_back(QStringLiteral("Source"), t.source);

    // Age (relative to last update) and current freshness. When the target is
    // gone from the store we use the empty target's defaults; show "Lost" so
    // the user knows the window is showing a snapshot of the last known data.
    const double age = (live && t.lastUpdateUtc.isValid())
        ? t.lastUpdateUtc.msecsTo(now) / 1000.0
        : (live ? 0.0 : -1.0);
    if (age >= 0.0)
        fields.emplace_back(QStringLiteral("Age"),
                            QString::number(age, 'f', 0) + QStringLiteral(" s"));
    fields.emplace_back(QStringLiteral("Status"),
                        fmtFreshness(live ? t.freshness : AisFreshness::Lost, lost_ || !live));

    // Grow / shrink the reusable row pool, then update in place — never tearing
    // down surviving rows, so the scroll position and any in-progress drag are
    // preserved and nothing flickers.
    const int want = int(fields.size());
    while (int(rows_.size()) < want) {
        Row r = makeRow();
        rowLayout_->insertWidget(rowLayout_->count() - 1, r.widget);
        rows_.push_back(r);
    }
    while (int(rows_.size()) > want) {
        rows_.back().widget->deleteLater();
        rows_.pop_back();
    }

    auto setText = [](QLabel* l, const QString& s) { if (l->text() != s) l->setText(s); };
    for (int i = 0; i < want; ++i) {
        setText(rows_[i].field, fields[i].first);
        setText(rows_[i].value, fields[i].second);
    }
}
