#include "ship_size_dialog.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <cmath>

namespace {
constexpr int kSliderMin = 2;
constexpr int kSliderMax = 12;

int    scaleToStep(double scale) { return static_cast<int>(std::lround(scale / 0.25)); }
double stepToScale(int step)     { return step * 0.25; }

QString formatScale(double scale) {
    int pct = static_cast<int>(std::lround(scale * 100.0));
    if (pct == 100) return QStringLiteral("100 % (normal)");
    return QString::number(pct) + QStringLiteral(" %");
}
} // namespace

ShipSizeDialog::ShipSizeDialog(double scale, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Ship Size"));
    resize(480, 280);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Scale the vessel glyph for both ownship and AIS targets. "
        "Larger values make ships easier to spot at a glance; smaller "
        "values reduce clutter in busy traffic."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* caption = new QLabel(QStringLiteral("Vessel size:"));
    caption->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(caption);

    slider_ = new QSlider(Qt::Horizontal);
    slider_->setMinimum(kSliderMin);
    slider_->setMaximum(kSliderMax);
    slider_->setSingleStep(1);
    slider_->setPageStep(1);
    slider_->setTickPosition(QSlider::TicksBelow);
    slider_->setTickInterval(1);
    slider_->setMinimumHeight(44);
    int step = scaleToStep(scale);
    if (step < kSliderMin) step = kSliderMin;
    if (step > kSliderMax) step = kSliderMax;
    slider_->setValue(step);
    col->addWidget(slider_);

    valueLabel_ = new QLabel;
    valueLabel_->setAlignment(Qt::AlignCenter);
    valueLabel_->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    col->addWidget(valueLabel_);
    updateValueLabel();
    connect(slider_, &QSlider::valueChanged, this, [this] { updateValueLabel(); });

    col->addStretch(1);

    auto* row = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    auto* okBtn     = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn}) b->setMinimumHeight(44);
    okBtn->setDefault(true);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(okBtn);
    col->addLayout(row);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
}

double ShipSizeDialog::vesselScale() const {
    return stepToScale(slider_->value());
}

void ShipSizeDialog::updateValueLabel() {
    valueLabel_->setText(formatScale(stepToScale(slider_->value())));
}
