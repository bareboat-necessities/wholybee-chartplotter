#pragma once
#include <QDialog>

class QSlider;
class QLabel;

// Touch-friendly editor for the vessel glyph scale (ownship + AIS targets).
// Same 11-stop slider as ChartSymbolSizeDialog: 50 % .. 300 % in 25 % steps.
class ShipSizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShipSizeDialog(double scale, QWidget* parent = nullptr);

    // Returns the selected scale factor (0.5 .. 3.0).
    double vesselScale() const;

private:
    void updateValueLabel();

    QSlider* slider_ = nullptr;
    QLabel*  valueLabel_ = nullptr;
};
