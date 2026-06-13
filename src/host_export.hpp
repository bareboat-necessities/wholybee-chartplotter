#pragma once
#include <QtCore/qglobal.h>

// HOST_EXPORT marks types whose definitions cross the host/plugin boundary.
//
// CMake's WINDOWS_EXPORT_ALL_SYMBOLS auto-exports function symbols from the
// host exe's object files but not data symbols — and a Q_OBJECT class has a
// static data member (`staticMetaObject`) that every signal/slot connection
// and qobject_cast<> needs to resolve. The plugin DLL therefore fails to link
// against the host's QObject classes unless we explicitly mark them.
//
// The macro flips between dllexport (when building the host exe; the host
// target defines CHARTPLOTTER_BUILD_HOST) and dllimport (when consumed from a
// plugin DLL). On Linux/macOS Q_DECL_EXPORT/IMPORT expand to default-visibility
// attributes and the symbol just works either way.
#if defined(CHARTPLOTTER_BUILD_HOST)
#  define HOST_EXPORT Q_DECL_EXPORT
#else
#  define HOST_EXPORT Q_DECL_IMPORT
#endif
