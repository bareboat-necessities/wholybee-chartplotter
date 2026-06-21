#pragma once
#include <QString>

// Single source of truth for the application's user-visible identity: the name
// shown in the title bar and About box, and the version string. The version is
// injected by the build from the CMake project version (APP_VERSION); the
// fallback keeps non-host translation units (which don't get the define) building.
namespace appinfo {

inline QString name() { return QStringLiteral("HMV Chartplotter"); }

inline QString version() {
#ifdef APP_VERSION
    return QStringLiteral(APP_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

} // namespace appinfo
