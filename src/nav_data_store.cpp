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
    emit thresholdsChanged();
    recomputeFreshness();
}

void NavDataStore::setInvalidSeconds(double s) {
    if (s == invalidSeconds_) return;
    invalidSeconds_ = s;
    emit thresholdsChanged();
    recomputeFreshness();
}

void NavDataStore::publishOwnshipPosition(double latDeg, double lonDeg,
                                          const NavValueMeta& meta) {
    ownship_.latitudeDeg  = latDeg;
    ownship_.longitudeDeg = lonDeg;
    ownship_.meta = meta;
    ownship_.meta.valid = true;
    ownship_.meta.ageSeconds = 0.0;
    recomputeFreshness();
    emit ownshipChanged();
}

void NavDataStore::publishCogSog(double cogDegTrue, double sogKnots,
                                 const NavValueMeta&) {
    ownship_.cogDegTrue = cogDegTrue;
    ownship_.sogKnots   = sogKnots;
    emit ownshipChanged();
}

void NavDataStore::publishHeading(double headingDegTrue,
                                  std::optional<double> headingDegMag,
                                  const NavValueMeta&) {
    ownship_.headingDegTrue = headingDegTrue;
    if (headingDegMag.has_value()) ownship_.headingDegMag = headingDegMag;
    emit ownshipChanged();
}

void NavDataStore::tick() {
    recomputeFreshness();
}

void NavDataStore::recomputeFreshness() {
    if (!ownship_.meta.timestampUtc.isValid()) {
        if (freshness_ != NavFreshness::Invalid) {
            freshness_ = NavFreshness::Invalid;
            emit freshnessChanged(freshness_);
        }
        return;
    }
    const double age = ownship_.meta.timestampUtc.msecsTo(
                           QDateTime::currentDateTimeUtc()) / 1000.0;
    ownship_.meta.ageSeconds = age;

    NavFreshness f;
    if      (age < staleSeconds_)   f = NavFreshness::Fresh;
    else if (age < invalidSeconds_) f = NavFreshness::Stale;
    else                            f = NavFreshness::Invalid;

    ownship_.meta.valid = (f != NavFreshness::Invalid);
    if (f != freshness_) {
        freshness_ = f;
        emit freshnessChanged(freshness_);
    }
}
