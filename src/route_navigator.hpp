#pragma once
#include <QObject>

class QTimer;
class NavDataStore;
class RouteStore;
class Settings;

// Drives route following. While active it ticks at ~1 Hz, tracks which leg of the
// route is active (auto-advancing as each waypoint is reached), computes the full
// set of APB/RMB navigation values, and writes them to the NavDataStore as a
// NavigationData snapshot. Reads ownship position/COG/SOG/variation from the same
// store and the arrival radius + true/magnetic preference from Settings.
//
// The route is looked up fresh each tick by id, so edits to (or deletion of) the
// route while navigating are handled — a deleted/empty route stops navigation.
class RouteNavigator : public QObject {
    Q_OBJECT
public:
    RouteNavigator(NavDataStore* nav, RouteStore* routes, Settings* settings,
                   QObject* parent = nullptr);

    bool   isActive() const { return active_; }
    qint64 routeId()  const { return routeId_; }
    // True if there is a remembered route that still exists to resume (used when
    // the user re-checks "Navigating" from the menu).
    bool   canResume() const;

public slots:
    // Begin (or restart) navigating a route. destIndex selects the first
    // destination ("next") waypoint — the point the user tapped, or the end of
    // the tapped leg. -1 (the default) starts on the first leg (point 0 -> 1).
    void startRoute(qint64 routeId, int destIndex = -1);
    void resume();                      // restart the last route, keeping its active leg
    void stop();                        // stop and clear the store's NavigationData

signals:
    void activeChanged(bool on);        // drives the menu "Navigating" checkbox
    // The boat reached the route's final waypoint: navigation has been stopped
    // (activeChanged(false) is emitted too). Lets the UI announce completion.
    void navigationCompleted();

private slots:
    void tick();

private:
    void recompute();

    NavDataStore* nav_ = nullptr;
    RouteStore*   routes_ = nullptr;
    Settings*     settings_ = nullptr;
    QTimer*       timer_ = nullptr;
    bool          active_ = false;
    qint64        routeId_ = -1;        // remembered across stop() so resume() works
    int           destIdx_ = 1;         // destination point index (origin = destIdx_-1)
};
