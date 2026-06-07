#pragma once
#include <QDialog>

class TouchSpinBox;

// Touch-friendly editor for the ownship course-prediction length (minutes),
// replacing the small QInputDialog prompt. Edits a working copy; the caller
// reads minutes() after acceptance and persists through Settings.
class OwnshipPredictionDialog : public QDialog {
    Q_OBJECT
public:
    explicit OwnshipPredictionDialog(double minutes, QWidget* parent = nullptr);

    double minutes() const;

private:
    TouchSpinBox* minutesBox_ = nullptr;
};
