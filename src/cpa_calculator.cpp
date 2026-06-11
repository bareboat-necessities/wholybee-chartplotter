#include "cpa_calculator.hpp"
#include "nav_data_store.hpp"
#include "ais_target_store.hpp"

#include <QTimer>
#include <QtMath>
#include <cmath>

namespace {
constexpr double kDeg2Rad        = M_PI / 180.0;
constexpr double kKnotToMs       = 0.514444;     // 1 knot in m/s
constexpr double kMetresPerDegLat = 111320.0;    // good enough for CPA ranges
} // namespace

CpaCalculator::CpaCalculator(NavDataStore* nav, AisTargetStore* ais, QObject* parent)
    : QObject(parent), nav_(nav), ais_(ais) {
    // A steady tick keeps TCPA counting down even when no new report arrives;
    // ownshipChanged recomputes promptly when the boat's own state moves. We do
    // NOT listen to targetUpdated — setCpaTcpa emits it, which would recurse.
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &CpaCalculator::recompute);
    timer_->start();
    if (nav_)
        connect(nav_, &NavDataStore::ownshipChanged, this, &CpaCalculator::recompute);
}

void CpaCalculator::setOwnshipMmsi(quint32 mmsi) {
    if (ownMmsi_ == mmsi) return;
    ownMmsi_ = mmsi;
    recompute();
}

void CpaCalculator::recompute() {
    if (!nav_ || !ais_) return;
    const OwnshipState& os = nav_->ownship();

    // Distance to ownship needs only a position fix; CPA/TCPA additionally need
    // the ownship's course and speed.
    const bool haveOwnPos = os.latitudeDeg.valid() && os.longitudeDeg.valid();
    const bool haveOwnVel = haveOwnPos && os.cogDegTrue.valid() && os.sogKnots.valid();

    if (!haveOwnPos) {
        // Without an ownship position there is nothing to measure against; clear
        // any previously published numbers (once) so the UI does not keep showing
        // a frozen range/CPA/TCPA.
        if (hadOwnship_) {
            const auto keys = ais_->targets().keys();
            for (quint32 mmsi : keys) {
                ais_->setRangeMeters(mmsi, std::nullopt);
                ais_->setCpaTcpa(mmsi, std::nullopt, std::nullopt);
            }
            hadOwnship_ = false;
        }
        return;
    }
    hadOwnship_ = true;

    const double ownLat = os.latitudeDeg.value;
    const double ownLon = os.longitudeDeg.value;
    const double mPerDegLon = kMetresPerDegLat * std::cos(ownLat * kDeg2Rad);

    // Ownship velocity in the east/north plane (m/s). COG is degrees clockwise
    // from true north, so east = sin, north = cos. Zero when course/speed are
    // unavailable — distances are still valid, only CPA/TCPA are withheld.
    const double ownCog = haveOwnVel ? os.cogDegTrue.value * kDeg2Rad : 0.0;
    const double ownSog = haveOwnVel ? os.sogKnots.value * kKnotToMs  : 0.0;
    const double ownVx = ownSog * std::sin(ownCog);
    const double ownVy = ownSog * std::cos(ownCog);

    // Snapshot the keys first: the setters emit targetUpdated, and while no
    // current slot mutates the store, iterating over a copy keeps this robust.
    const auto keys = ais_->targets().keys();
    for (quint32 mmsi : keys) {
        if (mmsi == ownMmsi_ && ownMmsi_ != 0) {
            ais_->setRangeMeters(mmsi, std::nullopt);
            ais_->setCpaTcpa(mmsi, std::nullopt, std::nullopt);
            continue;
        }
        const AisTarget* t = ais_->target(mmsi);
        if (!t) continue;
        if (!t->hasPosition()) {
            ais_->setRangeMeters(mmsi, std::nullopt);
            ais_->setCpaTcpa(mmsi, std::nullopt, std::nullopt);
            continue;
        }

        // Relative position of the target (target - own), east/north metres.
        const double dx = (*t->longitudeDeg - ownLon) * mPerDegLon;
        const double dy = (*t->latitudeDeg  - ownLat) * kMetresPerDegLat;
        const double range = std::sqrt(dx * dx + dy * dy);
        ais_->setRangeMeters(mmsi, range);

        // CPA/TCPA need both vessels' course & speed; without them, distance
        // alone is reported.
        if (!haveOwnVel || !t->cogDegTrue || !t->sogKnots) {
            ais_->setCpaTcpa(mmsi, std::nullopt, std::nullopt);
            continue;
        }

        const double tCog = *t->cogDegTrue * kDeg2Rad;
        const double tSog = *t->sogKnots * kKnotToMs;
        const double tVx = tSog * std::sin(tCog);
        const double tVy = tSog * std::cos(tCog);

        // Relative velocity (target - own).
        const double dvx = tVx - ownVx;
        const double dvy = tVy - ownVy;
        const double dv2 = dvx * dvx + dvy * dvy;

        if (dv2 < 1e-6) {
            // No relative motion: the range never changes, so CPA is the current
            // range and there is no finite time-to-CPA.
            ais_->setCpaTcpa(mmsi, range, std::nullopt);
            continue;
        }

        // Time of closest approach: the instant the relative-position vector is
        // perpendicular to the relative-velocity vector. Negative => already past.
        const double tcpa = -(dx * dvx + dy * dvy) / dv2;   // seconds
        const double cx = dx + dvx * tcpa;
        const double cy = dy + dvy * tcpa;
        const double cpa = std::sqrt(cx * cx + cy * cy);    // metres

        ais_->setCpaTcpa(mmsi, cpa, tcpa);
    }
}
