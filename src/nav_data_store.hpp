#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <optional>
#include "host_export.hpp"

class QTimer;

// Freshness of a single navigation value, derived from its age against the
// stale/invalid thresholds.
enum class NavFreshness { Fresh, Stale, Invalid };

// Provenance a publisher supplies when it sets a value: which source produced
// it and when. The store derives age/freshness from this.
struct NavValueMeta {
    QString   source;              // e.g. "nmea0183", "nmea2000", "simulator"
    QDateTime timestampUtc;        // when the source produced the value
};

// A single navigation value with its own provenance and freshness, so each
// field ages out independently and may come from a different source — e.g.
// position from NMEA 0183 while depth/wind arrive from NMEA 2000.
struct NavValue {
    double       value = 0.0;
    QString      source;                       // empty until first set
    QDateTime    timestampUtc;                 // invalid until first set
    double       ageSeconds = 0.0;             // maintained by the store
    NavFreshness freshness = NavFreshness::Invalid;

    bool   valid() const { return freshness != NavFreshness::Invalid; }
    bool   stale() const { return freshness == NavFreshness::Stale; }
    double valueOr(double fallback) const { return valid() ? value : fallback; }
};

// Live ownship navigation state: one self-describing value per field. A field
// whose freshness is Invalid is either never set or has aged out; consumers
// treat both as "not available". See ProjectSpec.md for the Signal K mapping.
struct OwnshipState {
    NavValue latitudeDeg;
    NavValue longitudeDeg;
    NavValue cogDegTrue;             // course over ground (true)
    NavValue sogKnots;               // speed over ground
    NavValue waterSpeedKnots;        // speed through the water
    NavValue headingDegTrue;
    NavValue headingDegMag;
    NavValue variationDeg;
    NavValue depthMeters;
    // Wind. Apparent and true are distinct quantities (relative to the bow);
    // true wind direction is geographic (relative to true north).
    NavValue apparentWindAngleDeg;   // relative to bow, 0..360 (0 = ahead)
    NavValue apparentWindSpeedKnots;
    NavValue trueWindAngleDeg;       // relative to bow, 0..360
    NavValue trueWindSpeedKnots;
    NavValue trueWindDirectionDeg;   // geographic (relative to true north)
};

// Stable API publishers (built-in or future dynamic plugins) call to publish
// navigation updates. Each call carries its own meta, so different fields can
// originate from different sources and age independently. The core owns the
// data and computes freshness; publishers never touch shared state directly.
class INavDataPublisher {
public:
    virtual ~INavDataPublisher() = default;
    virtual void publishOwnshipPosition(double latDeg, double lonDeg,
                                        const NavValueMeta& meta) = 0;
    virtual void publishCogSog(double cogDegTrue, double sogKnots,
                               const NavValueMeta& meta) = 0;
    // Heading: either component may be absent (e.g. HDT gives true only).
    virtual void publishHeading(std::optional<double> headingDegTrue,
                                std::optional<double> headingDegMag,
                                const NavValueMeta& meta) = 0;
    virtual void publishVariation(double variationDeg, const NavValueMeta& meta) = 0;
    virtual void publishDepth(double depthMeters, const NavValueMeta& meta) = 0;
    virtual void publishWaterSpeed(double knots, const NavValueMeta& meta) = 0;
    // Apparent / true wind angle (relative to the bow) + speed.
    virtual void publishApparentWind(double speedKnots, double angleDeg,
                                     const NavValueMeta& meta) = 0;
    virtual void publishTrueWind(double speedKnots, double angleDeg,
                                 const NavValueMeta& meta) = 0;
    // True wind direction (geographic) + speed.
    virtual void publishTrueWindDirection(double directionDeg, double speedKnots,
                                          const NavValueMeta& meta) = 0;
};

// Central navigation data store. Single source of truth for live nav state;
// publishers call publish*, consumers subscribe to ownshipChanged().
//
// Per-value staleness: a background tick ages every value and transitions it
// Fresh -> Stale -> Invalid as the thresholds elapse. ownshipChanged() fires on
// any publish and on any freshness transition, so consumers re-read and can
// render each value by its own freshness.
//
// Source arbitration: each value is taken from the highest-priority source. A
// lower-priority source can only overwrite a value once the current one becomes
// Invalid (aged out) — i.e. priority with fall-back. Priority is the ordered
// source-id list set via setSourcePriority() (highest first).
class HOST_EXPORT NavDataStore : public QObject, public INavDataPublisher {
    Q_OBJECT
public:
    explicit NavDataStore(QObject* parent = nullptr);

    const OwnshipState& ownship() const { return ownship_; }
    // Freshness of the position fix (drives the ownship symbol).
    NavFreshness positionFreshness() const { return ownship_.latitudeDeg.freshness; }
    double staleSeconds()   const { return staleSeconds_; }
    double invalidSeconds() const { return invalidSeconds_; }

    // INavDataPublisher --------------------------------------------------
    void publishOwnshipPosition(double latDeg, double lonDeg,
                                const NavValueMeta& meta) override;
    void publishCogSog(double cogDegTrue, double sogKnots,
                       const NavValueMeta& meta) override;
    void publishHeading(std::optional<double> headingDegTrue,
                        std::optional<double> headingDegMag,
                        const NavValueMeta& meta) override;
    void publishVariation(double variationDeg, const NavValueMeta& meta) override;
    void publishDepth(double depthMeters, const NavValueMeta& meta) override;
    void publishWaterSpeed(double knots, const NavValueMeta& meta) override;
    void publishApparentWind(double speedKnots, double angleDeg,
                             const NavValueMeta& meta) override;
    void publishTrueWind(double speedKnots, double angleDeg,
                         const NavValueMeta& meta) override;
    void publishTrueWindDirection(double directionDeg, double speedKnots,
                                  const NavValueMeta& meta) override;

public slots:
    void setStaleSeconds(double s);
    void setInvalidSeconds(double s);
    // Ordered source ids, highest priority first. Takes effect on subsequent
    // publishes (values switch as sources refresh / age out).
    void setSourcePriority(const QStringList& orderedSourceIds);

signals:
    void ownshipChanged();            // a value or a freshness transitioned

private slots:
    void tick();

private:
    bool setValue(NavValue& v, double value, const NavValueMeta& meta);  // true if accepted
    bool accept(const NavValue& current, const QString& source) const;   // arbitration
    int  rank(const QString& source) const;             // priority index (lower = higher)
    bool ageValue(NavValue& v, const QDateTime& now);   // true if freshness changed
    bool recompute();                                   // age all; true if any changed

    OwnshipState ownship_;
    double staleSeconds_   = 5.0;
    double invalidSeconds_ = 30.0;
    QStringList sourcePriority_;     // highest priority first
    QTimer* tickTimer_ = nullptr;
};
