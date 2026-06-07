#pragma once
#include <QDialog>

class TouchSpinBox;

// Touch-friendly editor for the ownship stale-data thresholds, replacing the
// two stacked QInputDialog prompts with one dialog. Edits working copies; the
// caller reads staleSeconds()/invalidSeconds() after acceptance and persists
// through Settings. The Invalid threshold is kept strictly above Stale.
class StaleThresholdsDialog : public QDialog {
    Q_OBJECT
public:
    StaleThresholdsDialog(double staleSeconds, double invalidSeconds,
                          QWidget* parent = nullptr);

    double staleSeconds() const;
    double invalidSeconds() const;

private:
    TouchSpinBox* staleBox_ = nullptr;
    TouchSpinBox* invalidBox_ = nullptr;
};
