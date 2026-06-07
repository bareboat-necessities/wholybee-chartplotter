#pragma once
#include <QString>

// Shared unit preferences. Kept in a tiny header (no Qt object, no .cpp) so the
// core Settings store, the Units dialog, and the chart view can all agree on the
// same enums without depending on one another. Persisted as short stable keys
// ("feet", "nm", …) rather than enum ordinals so the on-disk values survive
// reordering of the enums.

enum class DepthUnit { Feet, Meters };
enum class DistanceUnit { NauticalMiles, StatuteMiles, Kilometers };

namespace units {

constexpr double kMetersToFeet = 3.280839895;

// ---- depth ----------------------------------------------------------------

inline QString depthUnitKey(DepthUnit u) {
    return u == DepthUnit::Meters ? QStringLiteral("meters") : QStringLiteral("feet");
}
inline DepthUnit depthUnitFromKey(const QString& s, DepthUnit fallback = DepthUnit::Feet) {
    if (s == QStringLiteral("meters")) return DepthUnit::Meters;
    if (s == QStringLiteral("feet"))   return DepthUnit::Feet;
    return fallback;
}
inline QString depthUnitLabel(DepthUnit u) {
    return u == DepthUnit::Meters ? QStringLiteral("Meters (m)")
                                  : QStringLiteral("Feet (ft)");
}

// ---- distance (not yet consumed; stored for upcoming range/route work) -----

inline QString distanceUnitKey(DistanceUnit u) {
    switch (u) {
        case DistanceUnit::StatuteMiles: return QStringLiteral("mi");
        case DistanceUnit::Kilometers:   return QStringLiteral("km");
        case DistanceUnit::NauticalMiles: break;
    }
    return QStringLiteral("nm");
}
inline DistanceUnit distanceUnitFromKey(const QString& s,
                                        DistanceUnit fallback = DistanceUnit::NauticalMiles) {
    if (s == QStringLiteral("mi")) return DistanceUnit::StatuteMiles;
    if (s == QStringLiteral("km")) return DistanceUnit::Kilometers;
    if (s == QStringLiteral("nm")) return DistanceUnit::NauticalMiles;
    return fallback;
}
inline QString distanceUnitLabel(DistanceUnit u) {
    switch (u) {
        case DistanceUnit::StatuteMiles: return QStringLiteral("Statute miles (mi)");
        case DistanceUnit::Kilometers:   return QStringLiteral("Kilometers (km)");
        case DistanceUnit::NauticalMiles: break;
    }
    return QStringLiteral("Nautical miles (nm)");
}

} // namespace units
