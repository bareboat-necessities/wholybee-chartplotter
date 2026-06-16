#pragma once
#include <QDialog>

class QSlider;
class QLabel;

// Touch-friendly editor for the two chart-detail controls.
//
// Detail bias: 5 discrete stops (-1, -0.5, 0, 0.5, +1); each step shifts the
// visible-width-to-band mapping by half a band. 0 is the nominal mapping. This
// chooses how high-detail a chart cell is pulled in for the current zoom.
//
// Object detail (SCAMIN): 9 discrete stops mapping to -1.0 .. +1.0. This biases
// the S-57 SCAMIN test that hides point objects (symbols + soundings) as you
// zoom out. Far left hides all such objects; far right shows them all; the
// centre honours each object's SCAMIN at the current zoom.
//
// Edits a working copy; the caller reads detailLevel() / scaminLevel() after
// acceptance and persists through Settings.
class ChartDetailDialog : public QDialog {
    Q_OBJECT
public:
    ChartDetailDialog(double detailLevel, double scaminLevel,
                      QWidget* parent = nullptr);

    double detailLevel() const;
    double scaminLevel() const;

private:
    void updateDetailLabel();
    void updateScaminLabel();

    QSlider* slider_ = nullptr;            // detail bias
    QLabel*  valueLabel_ = nullptr;
    QSlider* scaminSlider_ = nullptr;      // object detail (SCAMIN)
    QLabel*  scaminValueLabel_ = nullptr;
};
