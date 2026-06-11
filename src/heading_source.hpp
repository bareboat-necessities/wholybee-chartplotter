#pragma once
#include <QString>

// What the ownship glyph points along: the vessel's true heading (where the bow
// points, from a compass / AIS heading sensor) or its course over ground (the
// track actually made good, from GPS). When the chosen source has no data the
// renderer falls back to the other so the glyph still shows a direction.
enum class HeadingSource { Heading, Cog };

namespace headingsrc {

inline QString key(HeadingSource s) {
    return s == HeadingSource::Cog ? QStringLiteral("cog")
                                   : QStringLiteral("heading");
}

inline HeadingSource fromKey(const QString& k, HeadingSource fallback) {
    if (k == QLatin1String("cog"))     return HeadingSource::Cog;
    if (k == QLatin1String("heading")) return HeadingSource::Heading;
    return fallback;
}

} // namespace headingsrc
