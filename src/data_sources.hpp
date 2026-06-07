#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// Registry of known navigation data sources, by the stable id each publisher
// stamps on its NavValueMeta.source. Used to give the priority dialog readable
// names and to reconcile a persisted priority order with the sources this build
// actually knows about.
struct DataSourceInfo { QString id; QString name; };

namespace datasources {

// Canonical sources, in the default priority order (highest first). Real
// position data outranks the simulator by default.
inline QList<DataSourceInfo> known() {
    return {
        { QStringLiteral("nmea0183"),  QStringLiteral("NMEA 0183") },
        { QStringLiteral("simulator"), QStringLiteral("Simulator") },
    };
}

inline QStringList knownIds() {
    QStringList ids;
    for (const DataSourceInfo& d : known()) ids << d.id;
    return ids;
}

inline QString displayName(const QString& id) {
    for (const DataSourceInfo& d : known())
        if (d.id == id) return d.name;
    return id;   // unknown source: show its raw id
}

// Reconcile a saved order with the known set: keep saved order for ids we still
// know, drop unknown ids, and append any known ids missing from the saved list
// (e.g. a source added in a newer build). Highest priority first.
inline QStringList reconcile(const QStringList& saved) {
    QStringList out;
    const QStringList ids = knownIds();
    for (const QString& s : saved)
        if (ids.contains(s) && !out.contains(s)) out << s;
    for (const QString& s : ids)
        if (!out.contains(s)) out << s;
    return out;
}

} // namespace datasources
