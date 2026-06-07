#pragma once
#include <QWidget>
#include <QString>

class QDoubleSpinBox;
class QPushButton;

// A touch-friendly numeric stepper: large minus/plus buttons flanking a
// QDoubleSpinBox. The spin box keeps keyboard entry, range clamping, decimals
// and a suffix; the big buttons replace its tiny native arrows for finger use.
class TouchSpinBox : public QWidget {
    Q_OBJECT
public:
    explicit TouchSpinBox(QWidget* parent = nullptr);

    void setRange(double lo, double hi);
    void setSingleStep(double step);
    void setDecimals(int decimals);
    void setSuffix(const QString& suffix);
    void setValue(double v);
    double value() const;

signals:
    void valueChanged(double v);

private:
    QDoubleSpinBox* spin_ = nullptr;
    QPushButton*    minus_ = nullptr;
    QPushButton*    plus_ = nullptr;
};
