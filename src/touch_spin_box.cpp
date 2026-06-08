#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QPushButton>

TouchSpinBox::TouchSpinBox(QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    // Pin all colours so the spin box and its +/- buttons read correctly on
    // both light and dark systems (Qt's default palette would otherwise leave
    // white text on this hard-coded white field).
    const theme::InputPalette& t = theme::input();

    auto makeStepButton = [&t](const QString& glyph) {
        auto* b = new QPushButton(glyph);
        b->setFixedSize(56, 56);            // generous finger target
        b->setAutoRepeat(true);             // hold to keep stepping
        b->setAutoRepeatDelay(350);
        b->setAutoRepeatInterval(90);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{ font-size:26px; font-weight:600; color:%1;"
            " border:1px solid %2; border-radius:8px; background:%3; }"
            "QPushButton:pressed{ background:%4; }")
            .arg(t.fg, t.border, t.buttonBg, t.pressed));
        return b;
    };
    minus_ = makeStepButton(QStringLiteral("−"));
    plus_  = makeStepButton(QStringLiteral("+"));

    spin_ = new QDoubleSpinBox;
    spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);   // use our big buttons
    spin_->setAlignment(Qt::AlignCenter);
    spin_->setMinimumHeight(56);
    spin_->setStyleSheet(QStringLiteral(
        "QDoubleSpinBox{ font-size:22px; padding:4px 8px; color:%1;"
        " border:1px solid %2; border-radius:8px; background:%3; }")
        .arg(t.fg, t.border, t.fieldBg));

    row->addWidget(minus_);
    row->addWidget(spin_, 1);
    row->addWidget(plus_);

    connect(minus_, &QPushButton::clicked, this, [this] { spin_->stepBy(-1); });
    connect(plus_,  &QPushButton::clicked, this, [this] { spin_->stepBy(+1); });
    connect(spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TouchSpinBox::valueChanged);
}

void TouchSpinBox::setRange(double lo, double hi) { spin_->setRange(lo, hi); }
void TouchSpinBox::setSingleStep(double step)     { spin_->setSingleStep(step); }
void TouchSpinBox::setDecimals(int decimals)      { spin_->setDecimals(decimals); }
void TouchSpinBox::setSuffix(const QString& s)    { spin_->setSuffix(s); }
void TouchSpinBox::setValue(double v)             { spin_->setValue(v); }
double TouchSpinBox::value() const                { return spin_->value(); }
