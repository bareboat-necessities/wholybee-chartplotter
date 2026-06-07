#include "ais_target_store.hpp"
#include <QTimer>

namespace {
// Copy an optional into the target only when the report actually supplies it,
// so a report that omits a field doesn't wipe a previously known value.
template <class T>
void applyIf(std::optional<T>& dst, const std::optional<T>& src) {
    if (src.has_value()) dst = src;
}
} // namespace

AisTargetStore::AisTargetStore(QObject* parent) : QObject(parent) {
    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(1000);   // 1 Hz is plenty for AIS aging
    connect(tickTimer_, &QTimer::timeout, this, &AisTargetStore::tick);
    tickTimer_->start();
}

const AisTarget* AisTargetStore::target(quint32 mmsi) const {
    auto it = targets_.constFind(mmsi);
    return it == targets_.constEnd() ? nullptr : &it.value();
}

AisTarget& AisTargetStore::touch(quint32 mmsi, AisClass cls, const QString& source) {
    AisTarget& t = targets_[mmsi];   // inserts a default target if new
    t.mmsi = mmsi;
    if (cls != AisClass::Unknown) t.cls = cls;
    t.source = source;
    t.lastUpdateUtc = QDateTime::currentDateTimeUtc();
    t.ageSeconds = 0.0;
    t.freshness = AisFreshness::Current;
    return t;
}

void AisTargetStore::publishAisPosition(const AisPositionReport& r, const QString& source) {
    if (r.mmsi == 0) return;
    AisTarget& t = touch(r.mmsi, r.cls, source);
    applyIf(t.latitudeDeg,    r.latitudeDeg);
    applyIf(t.longitudeDeg,   r.longitudeDeg);
    applyIf(t.cogDegTrue,     r.cogDegTrue);
    applyIf(t.sogKnots,       r.sogKnots);
    applyIf(t.headingDegTrue, r.headingDegTrue);
    applyIf(t.rotDegPerMin,   r.rotDegPerMin);
    if (r.navStatus != AisNavStatus::Undefined) t.navStatus = r.navStatus;
    emit targetUpdated(r.mmsi);
}

void AisTargetStore::publishAisStatic(const AisStaticData& d, const QString& source) {
    if (d.mmsi == 0) return;
    AisTarget& t = touch(d.mmsi, d.cls, source);
    if (!d.name.isEmpty())        t.name = d.name;
    if (!d.callSign.isEmpty())    t.callSign = d.callSign;
    if (!d.destination.isEmpty()) t.destination = d.destination;
    applyIf(t.shipType,      d.shipType);
    applyIf(t.imoNumber,     d.imoNumber);
    applyIf(t.draughtMeters, d.draughtMeters);
    if (d.dimensions.known()) t.dimensions = d.dimensions;
    emit targetUpdated(d.mmsi);
}

void AisTargetStore::setCpaTcpa(quint32 mmsi, std::optional<double> cpaMeters,
                                std::optional<double> tcpaSeconds) {
    auto it = targets_.find(mmsi);
    if (it == targets_.end()) return;
    it->cpaMeters = cpaMeters;
    it->tcpaSeconds = tcpaSeconds;
    emit targetUpdated(mmsi);
}

void AisTargetStore::setStaleSeconds(double s) { staleSeconds_ = s; }
void AisTargetStore::setLostSeconds(double s)  { lostSeconds_ = s; }

void AisTargetStore::tick() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<quint32> expired;

    for (auto it = targets_.begin(); it != targets_.end(); ++it) {
        AisTarget& t = it.value();
        t.ageSeconds = t.lastUpdateUtc.isValid()
            ? t.lastUpdateUtc.msecsTo(now) / 1000.0 : 0.0;

        AisFreshness f;
        if      (t.ageSeconds < staleSeconds_) f = AisFreshness::Current;
        else if (t.ageSeconds < lostSeconds_)  f = AisFreshness::Stale;
        else                                   f = AisFreshness::Lost;

        if (f == AisFreshness::Lost) {
            expired << it.key();
        } else if (f != t.freshness) {
            t.freshness = f;
            emit targetUpdated(it.key());
        }
    }

    for (quint32 mmsi : expired) {
        targets_.remove(mmsi);
        emit targetExpired(mmsi);
    }
}
