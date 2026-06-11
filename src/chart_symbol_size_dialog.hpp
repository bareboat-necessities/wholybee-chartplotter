#pragma once
#include <QDialog>

class QSlider;
class QLabel;

// Touch-friendly editor for the chart symbol scale. The slider has 11 discrete
// stops (50 % .. 300 % in 25 % steps); 100 % (scale 1.0) is the default.
//
// Edits a working copy; the caller reads symbolScale() after acceptance and
// persists through Settings.
class ChartSymbolSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChartSymbolSizeDialog(double scale, QWidget* parent = nullptr);

    // Returns the selected scale factor (0.5 .. 3.0).
    double symbolScale() const;

private:
    void updateValueLabel();

    QSlider* slider_ = nullptr;
    QLabel*  valueLabel_ = nullptr;
};
