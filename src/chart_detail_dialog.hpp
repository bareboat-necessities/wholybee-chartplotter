#pragma once
#include <QDialog>

class QSlider;
class QLabel;

// Touch-friendly editor for the chart detail bias. The slider has 5 discrete
// stops (-1, -0.5, 0, 0.5, +1); each step shifts the visible-width-to-band
// mapping by half a band. 0 is the nominal mapping (current behaviour).
//
// Edits a working copy; the caller reads detailLevel() after acceptance and
// persists through Settings.
class ChartDetailDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChartDetailDialog(double detailLevel, QWidget* parent = nullptr);

    double detailLevel() const;

private:
    void updateValueLabel();

    QSlider* slider_ = nullptr;
    QLabel*  valueLabel_ = nullptr;
};
