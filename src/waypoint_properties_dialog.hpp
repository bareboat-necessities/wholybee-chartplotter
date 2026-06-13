#pragma once
#include <QDialog>
#include "route_types.hpp"

class QLineEdit;

// Properties editor for a single waypoint: name, description, and a manually
// entered latitude/longitude (shown and parsed in the user's selected coordinate
// format). Operates on a working copy; the caller reads currentWaypoint() after
// the dialog is accepted and persists it. Dragging a waypoint on the chart is
// handled separately by the "Edit Waypoint" menu action.
class WaypointPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    WaypointPropertiesDialog(const Waypoint& wpt, QWidget* parent = nullptr);

    // Edited state (keeps the original id / created / symbol / visible).
    Waypoint currentWaypoint() const;

private:
    void onOk();

    Waypoint   work_;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* descEdit_ = nullptr;
    QLineEdit* latEdit_  = nullptr;
    QLineEdit* lonEdit_  = nullptr;
};
