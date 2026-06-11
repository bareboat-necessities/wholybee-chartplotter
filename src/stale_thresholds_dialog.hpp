#pragma once
#include <QDialog>

class TouchSpinBox;

// Touch-friendly editor for ownship and AIS stale-data thresholds.
// Ownship thresholds are in seconds; AIS thresholds are displayed in minutes
// (stored/returned as seconds). The Lost/Invalid threshold is always kept
// strictly above Stale in each section.
class StaleThresholdsDialog : public QDialog {
    Q_OBJECT
public:
    StaleThresholdsDialog(double staleSeconds, double invalidSeconds,
                          double aisStaleSeconds, double aisLostSeconds,
                          QWidget* parent = nullptr);

    double staleSeconds()    const;
    double invalidSeconds()  const;
    double aisStaleSeconds() const;   // returned in seconds
    double aisLostSeconds()  const;   // returned in seconds

private:
    TouchSpinBox* staleBox_    = nullptr;
    TouchSpinBox* invalidBox_  = nullptr;
    TouchSpinBox* aisStaleBox_ = nullptr;   // displays minutes
    TouchSpinBox* aisLostBox_  = nullptr;   // displays minutes
};
