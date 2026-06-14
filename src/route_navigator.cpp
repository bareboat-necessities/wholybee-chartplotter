#include "route_navigator.hpp"
#include "nav_data_store.hpp"
#include "route_store.hpp"
#include "settings.hpp"
#include "geo_nav.hpp"
#include "units.hpp"

#include <QTimer>
#include <cmath>

namespace {
constexpr double kArrivalEps = 1e-9;

// Name (if any) or 1-based number of a route point — the "waypoint ID" used in
// the APB/RMB origin/destination fields.
QString pointId(const QVector<RoutePoint>& pts, int i) {
    return pts[i].name.isEmpty() ? QString::number(i + 1) : pts[i].name;
}
}  // namespace

RouteNavigator::RouteNavigator(NavDataStore* nav, RouteStore* routes, Settings* settings,
                               QObject* parent)
    : QObject(parent), nav_(nav), routes_(routes), settings_(settings) {
    timer_ = new QTimer(this);
    timer_->setInterval(1000);   // ~1 Hz; not critical to be exact
    connect(timer_, &QTimer::timeout, this, &RouteNavigator::tick);
}

bool RouteNavigator::canResume() const {
    if (routeId_ < 0 || !routes_) return false;
    const Route* r = routes_->route(routeId_);
    return r && !r->points.isEmpty();
}

void RouteNavigator::startRoute(qint64 routeId, int destIndex) {
    if (!routes_) return;
    const Route* r = routes_->route(routeId);
    if (!r || r->points.isEmpty()) return;   // nothing navigable; leave state as-is
    const int n = r->points.size();

    routeId_ = routeId;
    if (destIndex >= 0 && destIndex < n) {
        // The user picked the destination by tapping a waypoint/leg. Index 0
        // means "head to the start", with the present position as origin.
        destIdx_ = destIndex;
    } else {
        // Default: start on the first leg (origin = point 0, destination = 1). A
        // single-point route has destination = point 0 and no origin leg.
        destIdx_ = n >= 2 ? 1 : 0;
    }

    const bool wasActive = active_;
    active_ = true;
    timer_->start();
    recompute();
    if (!wasActive) emit activeChanged(true);
}

void RouteNavigator::resume() {
    if (canResume()) startRoute(routeId_, destIdx_);   // keep the active leg
}

void RouteNavigator::stop() {
    if (!active_) return;
    active_ = false;
    timer_->stop();
    if (nav_) nav_->clearNavigation();   // routeId_ kept so resume() can restart
    emit activeChanged(false);
}

void RouteNavigator::tick() { recompute(); }

void RouteNavigator::recompute() {
    if (!active_ || !nav_ || !routes_ || !settings_) return;

    const Route* r = routes_->route(routeId_);
    if (!r || r->points.isEmpty()) { stop(); return; }   // route gone/empty
    const QVector<RoutePoint>& pts = r->points;
    const int n = pts.size();

    const OwnshipState& os = nav_->ownship();
    const bool haveFix = os.latitudeDeg.valid() && os.longitudeDeg.valid();
    const double ownLat = os.latitudeDeg.value;
    const double ownLon = os.longitudeDeg.value;

    // Clamp the active destination into range. destIdx_ may be 0, meaning the
    // boat is heading to the route's first point with the present position as the
    // (implicit) origin — e.g. the user tapped the start waypoint.
    if (destIdx_ < 0)     destIdx_ = (n >= 2) ? 1 : 0;
    if (destIdx_ > n - 1) destIdx_ = n - 1;

    // Auto-advance past any waypoints already reached: a waypoint is reached when
    // the boat is inside its arrival circle, or — when there is an origin leg —
    // when it has crossed the perpendicular through that waypoint. Never advance
    // past the final point.
    if (haveFix) {
        const double arrivalNm = settings_->arrivalRadiusNm();
        while (destIdx_ < n - 1) {
            const RoutePoint& dst = pts[destIdx_];
            bool reached = geonav::distanceNm(ownLat, ownLon, dst.lat, dst.lon) <= arrivalNm;
            if (!reached && destIdx_ >= 1) {
                const RoutePoint& org = pts[destIdx_ - 1];
                const double legLen = geonav::distanceNm(org.lat, org.lon, dst.lat, dst.lon);
                const double along  = geonav::alongTrackNm(ownLat, ownLon,
                                                           org.lat, org.lon, dst.lat, dst.lon);
                reached = legLen > kArrivalEps && along >= legLen;
            }
            if (reached) ++destIdx_;
            else break;
        }
    }

    // Resolve the active leg's origin and destination. With no prior route
    // waypoint (destIdx_ == 0) the present position acts as the origin.
    const RoutePoint& dest = pts[destIdx_];
    const bool haveOriginLeg = (destIdx_ >= 1);
    double orgLat = ownLat, orgLon = ownLon;
    if (haveOriginLeg) { orgLat = pts[destIdx_ - 1].lat; orgLon = pts[destIdx_ - 1].lon; }

    const BearingMode mode = settings_->bearingMode();
    // Local magnetic variation (easterly positive). Prefer a published variation;
    // otherwise derive it from true vs magnetic heading if both are present.
    double variation = 0.0;
    if (os.variationDeg.valid())
        variation = os.variationDeg.value;
    else if (os.headingDegTrue.valid() && os.headingDegMag.valid())
        variation = os.headingDegTrue.value - os.headingDegMag.value;
    const auto toMode = [&](double trueDeg) {
        return mode == BearingMode::Magnetic ? geonav::magneticFromTrue(trueDeg, variation)
                                             : geonav::norm360(trueDeg);
    };

    NavigationData d;
    d.active   = true;
    d.xteUnits = QLatin1Char('N');
    d.faaStatus = QLatin1Char('A');
    d.faaMode   = QLatin1Char('A');
    d.bearingUnits = (mode == BearingMode::Magnetic) ? QLatin1Char('M') : QLatin1Char('T');

    d.destinationWaypointId = pointId(pts, destIdx_);
    d.originWaypointId      = haveOriginLeg ? pointId(pts, destIdx_ - 1) : QString();
    d.destinationLatDeg = dest.lat;
    d.destinationLonDeg = dest.lon;

    // Bearing origin -> destination. For a multi-point route this is a fixed
    // waypoint-to-waypoint bearing (available even without a fix); for a single
    // point it degenerates to the present-position bearing (needs a fix).
    if (haveOriginLeg)
        d.bearingOriginToDestDeg = toMode(geonav::initialBearingDeg(orgLat, orgLon,
                                                                    dest.lat, dest.lon));

    if (haveFix) {
        d.rangeToDestNm = geonav::distanceNm(ownLat, ownLon, dest.lat, dest.lon);

        const double brgPresentTrue = geonav::initialBearingDeg(ownLat, ownLon,
                                                                dest.lat, dest.lon);
        d.bearingPresentToDestDeg = toMode(brgPresentTrue);
        d.headingToSteerDeg       = d.bearingPresentToDestDeg;   // same as bearing for now
        if (!haveOriginLeg)
            d.bearingOriginToDestDeg = d.bearingPresentToDestDeg;

        // Cross-track error and steer direction relative to the active leg.
        if (haveOriginLeg) {
            const double xt = geonav::crossTrackNm(ownLat, ownLon,
                                                   orgLat, orgLon, dest.lat, dest.lon);
            d.xteNm = std::abs(xt);
            d.steerDirection = (xt > 0.0) ? QLatin1Char('L') : QLatin1Char('R');
            const double legLen = geonav::distanceNm(orgLat, orgLon, dest.lat, dest.lon);
            const double along  = geonav::alongTrackNm(ownLat, ownLon,
                                                       orgLat, orgLon, dest.lat, dest.lon);
            d.perpendicularPassed = legLen > kArrivalEps && along >= legLen;
        }

        d.arrivalCircleEntered = d.rangeToDestNm <= settings_->arrivalRadiusNm();

        // Closing velocity = VMG toward the destination = SOG * cos(bearing - COG).
        if (os.sogKnots.valid() && os.cogDegTrue.valid()) {
            const double ang = (brgPresentTrue - os.cogDegTrue.value) * geonav::D2R;
            d.closingVelocityKn = os.sogKnots.value * std::cos(ang);
        }
    }

    nav_->setNavigationData(d);
}
