#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include <QSqlDatabase>
#include "route_types.hpp"
#include "host_export.hpp"

// Persistent store for routes and standalone waypoints, backed by a read-write
// SQLite database (routes.db in the app data dir). The whole collection is held
// in memory (loaded once at open) so the chart overlay and list dialogs read
// without touching SQLite per frame; writes update both the cache and the DB and
// then emit routesChanged()/waypointsChanged() so consumers refresh.
//
// Thousands of routes/waypoints in QVectors is trivial in memory; the overlay
// culls to the viewport at paint time.
// HOST_EXPORT so the symbols resolve when a runtime plugin (e.g. gpx_plugin)
// links against the host: MinGW's auto-export does not cover class member
// functions, so RouteStore is marked explicitly like NavDataStore.
class HOST_EXPORT RouteStore : public QObject {
    Q_OBJECT
public:
    explicit RouteStore(QObject* parent = nullptr);
    ~RouteStore() override;

    bool isOpen() const { return ok_; }

    // Snapshots (cached). Returned by const-ref; copy if you need to mutate.
    const QVector<Waypoint>& waypoints() const { return waypoints_; }
    const QVector<Route>&    routes()    const { return routes_; }
    const Route* route(qint64 id) const;             // nullptr if absent

    // Suggested default name for a new route/waypoint ("Route 3", "Waypoint 7").
    // Numbers the next slot above any existing "{prefix} N" — so deleting and
    // re-creating doesn't collide; names with custom text are ignored.
    QString nextRouteName()    const;
    QString nextWaypointName() const;

    // Waypoints ---------------------------------------------------------------
    qint64 addWaypoint(Waypoint w);                  // returns new id (-1 on fail)
    void   updateWaypoint(const Waypoint& w);        // by w.id
    void   setWaypointVisible(qint64 id, bool on);
    void   removeWaypoint(qint64 id);

    // Routes ------------------------------------------------------------------
    qint64 addRoute(Route r);                        // inserts route + points
    void   updateRoute(const Route& r);              // replaces points (txn)
    void   setRouteVisible(qint64 id, bool on);
    void   removeRoute(qint64 id);

signals:
    void waypointsChanged();
    void routesChanged();

private:
    bool createSchema(QString& err);
    void loadAll();

    QSqlDatabase db_;
    QString      conn_;
    bool         ok_ = false;
    QVector<Waypoint> waypoints_;
    QVector<Route>    routes_;
};
