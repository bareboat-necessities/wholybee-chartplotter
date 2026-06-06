#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <optional>

// Per-value provenance: where this navigation value came from, when it was
// produced, and whether the receiver still considers it usable. Tracked
// per-value so individual fields can age out independently of each other (e.g.
// heading goes stale while position is still fresh).
struct NavValueMeta {
    QString   source;              // e.g. "simulator", "nmea0183.serial1"
    QDateTime timestampUtc;        // when the source produced this value
    double    ageSeconds = 0.0;    // cached; updated by NavDataStore
    bool      valid = false;       // age within the invalid threshold
};

// Live ownship navigation state. Optional fields are absent when the active
// source hasn't supplied them. Inspired by Signal K path names but stored as
// typed C++; see ProjectSpec.md for the conceptual mapping.
struct OwnshipState {
    std::optional<double> latitudeDeg;
    std::optional<double> longitudeDeg;
    std::optional<double> cogDegTrue;       // course over ground (true)
    std::optional<double> sogKnots;         // speed over ground
    std::optional<double> headingDegTrue;
    std::optional<double> headingDegMag;
    std::optional<double> variationDeg;
    std::optional<double> depthMeters;
    std::optional<double> windSpeedKnots;
    std::optional<double> windAngleDeg;
    NavValueMeta meta;              // applies to the position fix (extend later)
};

// Freshness of the ownship fix, derived from staleSeconds + invalidSeconds.
enum class NavFreshness { Fresh, Stale, Invalid };

// Stable API plugins (built-in or future dynamic) call to publish navigation
// updates. The core owns the data and reconciles sources/timestamps/validity;
// plugins never reach into shared state directly. Matches the publish/subscribe
// contract in ProjectSpec.md.
class INavDataPublisher {
public:
    virtual ~INavDataPublisher() = default;
    virtual void publishOwnshipPosition(double latDeg, double lonDeg,
                                        const NavValueMeta& meta) = 0;
    virtual void publishCogSog(double cogDegTrue, double sogKnots,
                               const NavValueMeta& meta) = 0;
    virtual void publishHeading(double headingDegTrue,
                                std::optional<double> headingDegMag,
                                const NavValueMeta& meta) = 0;
};

// Central navigation data store. Single source of truth for live nav state;
// publishers call set*, consumers subscribe to ownshipChanged().
//
// Stale-data tracking: a background tick updates ageSeconds on the current
// meta and transitions freshness through Fresh -> Stale -> Invalid as the
// configured thresholds elapse. Consumers can render the ownship symbol
// differently in each state (dimmed when stale, hidden/outlined when invalid).
class NavDataStore : public QObject, public INavDataPublisher {
    Q_OBJECT
public:
    explicit NavDataStore(QObject* parent = nullptr);

    const OwnshipState& ownship() const { return ownship_; }
    NavFreshness freshness() const { return freshness_; }
    double staleSeconds()   const { return staleSeconds_; }
    double invalidSeconds() const { return invalidSeconds_; }

    // INavDataPublisher --------------------------------------------------
    void publishOwnshipPosition(double latDeg, double lonDeg,
                                const NavValueMeta& meta) override;
    void publishCogSog(double cogDegTrue, double sogKnots,
                       const NavValueMeta& meta) override;
    void publishHeading(double headingDegTrue,
                        std::optional<double> headingDegMag,
                        const NavValueMeta& meta) override;

public slots:
    void setStaleSeconds(double s);
    void setInvalidSeconds(double s);

signals:
    void ownshipChanged();            // any ownship field updated
    void freshnessChanged(NavFreshness f);
    void thresholdsChanged();

private slots:
    void tick();

private:
    void recomputeFreshness();        // updates meta.age + freshness_

    OwnshipState ownship_;
    NavFreshness freshness_ = NavFreshness::Invalid;
    double staleSeconds_   = 5.0;
    double invalidSeconds_ = 30.0;
    class QTimer* tickTimer_ = nullptr;
};
