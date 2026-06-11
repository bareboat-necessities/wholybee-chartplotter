#include "chart_detail_dialog.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <cmath>

namespace {
// The slider runs in integer half-band steps so the tick marks land exactly on
// the requested stops. Range -2..+2 maps to -1.0, -0.5, 0.0, +0.5, +1.0.
constexpr int kSliderMin = -2;
constexpr int kSliderMax =  2;

int   levelToStep(double level) { return static_cast<int>(std::lround(level)); }
double stepToLevel(int step)     { return static_cast<double>(step); }

QString formatLevel(double level) {
    if (level == 0.0) return QStringLiteral("0 (normal)");
    return (level > 0.0 ? QStringLiteral("+") : QString())
         + QString::number(level, 'f', 1);
}
} // namespace

ChartDetailDialog::ChartDetailDialog(double detailLevel, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Chart Detail Level"));
    resize(480, 280);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Adjust how much chart detail is shown at the current zoom. Higher "
        "values pull in higher-detail charts; lower values back off to less "
        "detail. Zoom is unchanged."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* caption = new QLabel(QStringLiteral("Detail bias:"));
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
    int step = levelToStep(detailLevel);
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

double ChartDetailDialog::detailLevel() const {
    return stepToLevel(slider_->value());
}

void ChartDetailDialog::updateValueLabel() {
    valueLabel_->setText(formatLevel(stepToLevel(slider_->value())));
}
