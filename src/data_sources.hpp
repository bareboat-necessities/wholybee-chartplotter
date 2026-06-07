#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// One navigation data source, identified by the stable id its publisher stamps
// on NavValueMeta.source, plus a human-readable name for the UI.
struct DataSourceInfo { QString id; QString name; };

// Runtime registry of navigation sources — built-ins (NMEA 0183, Simulator)
// registered at startup, plus any registered by plugins. Drives the Data
// Priority dialog and the order applied to the NavDataStore, so plugin sources
// participate in priority/fall-back like built-in ones.
class DataSourceRegistry {
public:
    void add(const QString& id, const QString& name) {
        for (const DataSourceInfo& s : sources_)
            if (s.id == id) return;                 // ignore duplicate ids
        sources_.push_back({ id, name });
    }

    QString name(const QString& id) const {
        for (const DataSourceInfo& s : sources_)
            if (s.id == id) return s.name;
        return id;
    }

    // Registered sources in the saved priority order: ids that appear in `saved`
    // first (in that order), then any remaining registered sources (registration
    // order). Saved ids that are no longer registered are dropped.
    QList<DataSourceInfo> ordered(const QStringList& saved) const {
        QList<DataSourceInfo> out;
        QStringList used;
        for (const QString& id : saved)
            for (const DataSourceInfo& s : sources_)
                if (s.id == id && !used.contains(id)) { out.push_back(s); used << id; }
        for (const DataSourceInfo& s : sources_)
            if (!used.contains(s.id)) { out.push_back(s); used << s.id; }
        return out;
    }

    QStringList orderedIds(const QStringList& saved) const {
        QStringList ids;
        for (const DataSourceInfo& s : ordered(saved)) ids << s.id;
        return ids;
    }

private:
    QList<DataSourceInfo> sources_;   // registration order
};
