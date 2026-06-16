#pragma once
#include <QString>
#include <QDateTime>
#include <QVector>

// Plain data types for the routes/waypoints subsystem. Field names and types are
// chosen to map cleanly onto GPX (deferred), so an exporter can later emit
// <wpt>/<rte>/<rtept> without reshaping the model:
//   Waypoint     -> <wpt lat lon> + <name>/<sym>/<desc>/<time>
//   Route        -> <rte> + <name>/<desc>
//   RoutePoint   -> <rtept lat lon> + <name>
//
// id == -1 marks a record not yet persisted (no SQLite row). createdUtc stores
// the GPX <time> instant in UTC.

struct Waypoint {
    qint64    id = -1;
    QString   name;
    double    lat = 0.0;
    double    lon = 0.0;
    QString   symbol;        // GPX <sym>
    QString   description;   // GPX <desc>
    QDateTime createdUtc;    // GPX <time>
    bool      visible = true;
};

struct RoutePoint {
    double  lat = 0.0;
    double  lon = 0.0;
    QString name;            // GPX <rtept><name>, optional
};

struct Route {
    qint64    id = -1;
    QString   name;
    QString   description;
    QDateTime createdUtc;
    bool      visible = true;
    QVector<RoutePoint> points;   // ordered (seq)
};
