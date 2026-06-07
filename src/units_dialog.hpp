#pragma once
#include <QDialog>
#include "units.hpp"

class QButtonGroup;

// Touch-friendly chooser for display units (depth + distance).
//
// Edits a working copy; the caller reads depthUnit()/distanceUnit() after the
// dialog is accepted and persists through Settings. The dialog never touches
// Settings directly.
class UnitsDialog : public QDialog {
    Q_OBJECT
public:
    UnitsDialog(DepthUnit depth, DistanceUnit distance, QWidget* parent = nullptr);

    DepthUnit    depthUnit()    const;
    DistanceUnit distanceUnit() const;

private:
    QButtonGroup* depthGroup_ = nullptr;
    QButtonGroup* distGroup_  = nullptr;
};
