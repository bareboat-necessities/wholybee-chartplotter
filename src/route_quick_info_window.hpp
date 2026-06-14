#pragma once
#include <QFrame>
#include "route_overlay.hpp"   // ClickedRouteObject::Kind

class RouteStore;
class QLabel;

// Quick-look popup for a route or waypoint the user tapped on the chart.
//
// Mirrors AisQuickInfoWindow: frameless tool window shown next to the tap, no
// focus grab so chart pans still dismiss it. Exposes the small set of actions
// most users want without trip through the side menu: rename, edit on chart
// (drag), open the Properties editor, toggle visibility, delete.
//
// Modeless and one-shot — the host (MainWindow) keeps a QPointer to it, swaps
// it for a fresh one on the next tap, and listens for the action signals to
// drive the existing handlers.
class RouteQuickInfoWindow : public QFrame {
    Q_OBJECT
public:
    RouteQuickInfoWindow(ClickedRouteObject::Kind kind, qint64 id,
                         const RouteStore* store, QWidget* parent = nullptr);

    ClickedRouteObject::Kind kind() const { return kind_; }
    qint64 id() const { return id_; }

signals:
    void renameRequested();
    void editRequested();        // route: "Edit on chart"; waypoint: "Drag on chart"
    void propertiesRequested();
    void visibilityToggleRequested();
    void deleteRequested();

private slots:
    void refresh();

private:
    const ClickedRouteObject::Kind kind_;
    const qint64       id_;
    const RouteStore*  store_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subLabel_   = nullptr;
    class QPushButton* visBtn_ = nullptr;   // text flips Hide/Show
};
