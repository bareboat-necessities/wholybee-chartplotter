#pragma once
#include <QDialog>

class TouchSpinBox;

// Touch-friendly editor for navigation options. Currently a single setting —
// the arrival radius (nautical miles) used to decide when the boat has reached
// a waypoint. Edits a working copy; the caller reads arrivalRadiusNm() after
// acceptance and persists through Settings.
class NavigationOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit NavigationOptionsDialog(double arrivalRadiusNm, QWidget* parent = nullptr);

    double arrivalRadiusNm() const;

private:
    TouchSpinBox* arrivalBox_ = nullptr;
};
