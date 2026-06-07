#include "nav_data_store.hpp"
#include <QTimer>
#include <QDateTime>

NavDataStore::NavDataStore(QObject* parent) : QObject(parent) {
    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(500);   // 2 Hz is plenty for stale-data UI
    connect(tickTimer_, &QTimer::timeout, this, &NavDataStore::tick);
    tickTimer_->start();
}

void NavDataStore::setStaleSeconds(double s) {
    if (s == staleSeconds_) return;
    staleSeconds_ = s;
    if (recompute()) emit ownshipChanged();
}

void NavDataStore::setInvalidSeconds(double s) {
    if (s == invalidSeconds_) return;
    invalidSeconds_ = s;
    if (recompute()) emit ownshipChanged();
}

// ---- publishing ------------------------------------------------------------

void NavDataStore::setValue(NavValue& v, double value, const NavValueMeta& meta) {
    v.value        = value;
    v.source       = meta.source;
    v.timestampUtc = meta.timestampUtc.isValid() ? meta.timestampUtc
                                                 : QDateTime::currentDateTimeUtc();
    v.ageSeconds   = 0.0;
    v.freshness    = NavFreshness::Fresh;
}

void NavDataStore::publishOwnshipPosition(double latDeg, double lonDeg,
                                          const NavValueMeta& meta) {
    setValue(ownship_.latitudeDeg,  latDeg, meta);
    setValue(ownship_.longitudeDeg, lonDeg, meta);
    emit ownshipChanged();
}

void NavDataStore::publishCogSog(double cogDegTrue, double sogKnots,
                                 const NavValueMeta& meta) {
    setValue(ownship_.cogDegTrue, cogDegTrue, meta);
    setValue(ownship_.sogKnots,   sogKnots,   meta);
    emit ownshipChanged();
}

void NavDataStore::publishHeading(double headingDegTrue,
                                  std::optional<double> headingDegMag,
                                  const NavValueMeta& meta) {
    setValue(ownship_.headingDegTrue, headingDegTrue, meta);
    if (headingDegMag.has_value())
        setValue(ownship_.headingDegMag, *headingDegMag, meta);
    emit ownshipChanged();
}

void NavDataStore::publishDepth(double depthMeters, const NavValueMeta& meta) {
    setValue(ownship_.depthMeters, depthMeters, meta);
    emit ownshipChanged();
}

void NavDataStore::publishWind(double windSpeedKnots, double windAngleDeg,
                               const NavValueMeta& meta) {
    setValue(ownship_.windSpeedKnots, windSpeedKnots, meta);
    setValue(ownship_.windAngleDeg,   windAngleDeg,   meta);
    emit ownshipChanged();
}

void NavDataStore::publishVariation(double variationDeg, const NavValueMeta& meta) {
    setValue(ownship_.variationDeg, variationDeg, meta);
    emit ownshipChanged();
}

// ---- aging -----------------------------------------------------------------

bool NavDataStore::ageValue(NavValue& v, const QDateTime& now) {
    if (!v.timestampUtc.isValid()) {           // never set
        if (v.freshness != NavFreshness::Invalid) {
            v.freshness = NavFreshness::Invalid;
            return true;
        }
        return false;
    }
    v.ageSeconds = v.timestampUtc.msecsTo(now) / 1000.0;
    NavFreshness f;
    if      (v.ageSeconds < staleSeconds_)   f = NavFreshness::Fresh;
    else if (v.ageSeconds < invalidSeconds_) f = NavFreshness::Stale;
    else                                     f = NavFreshness::Invalid;

    if (f != v.freshness) { v.freshness = f; return true; }
    return false;
}

bool NavDataStore::recompute() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    NavValue* all[] = {
        &ownship_.latitudeDeg,  &ownship_.longitudeDeg, &ownship_.cogDegTrue,
        &ownship_.sogKnots,     &ownship_.headingDegTrue, &ownship_.headingDegMag,
        &ownship_.variationDeg, &ownship_.depthMeters,  &ownship_.windSpeedKnots,
        &ownship_.windAngleDeg,
    };
    bool changed = false;
    for (NavValue* v : all) changed |= ageValue(*v, now);
    return changed;
}

void NavDataStore::tick() {
    if (recompute()) emit ownshipChanged();
}
