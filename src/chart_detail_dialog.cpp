#include "chart_detail_dialog.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <cmath>

namespace {
// The detail slider runs in integer half-band steps so the tick marks land
// exactly on the requested stops. Range -2..+2 maps to -1.0, -0.5, 0.0, +0.5,
// +1.0.
constexpr int kSliderMin = -2;
constexpr int kSliderMax =  2;

int    levelToStep(double level) { return static_cast<int>(std::lround(level)); }
double stepToLevel(int step)     { return static_cast<double>(step); }

QString formatLevel(double level) {
    if (level == 0.0) return QStringLiteral("0 (normal)");
    return (level > 0.0 ? QStringLiteral("+") : QString())
         + QString::number(level, 'f', 1);
}

// The SCAMIN slider has 9 integer stops; each maps to one quarter, so the range
// -4..+4 covers -1.0 .. +1.0 (the level Settings/ChartView expect). The end
// stops are the hard "hide all" / "show all" cases.
constexpr int kScaminStepMin = -4;
constexpr int kScaminStepMax =  4;

int    scaminLevelToStep(double level) {
    return static_cast<int>(std::lround(level * 4.0));
}
double scaminStepToLevel(int step) { return step / 4.0; }

QString formatScamin(int step) {
    if (step <= kScaminStepMin) return QStringLiteral("Hide all objects");
    if (step >= kScaminStepMax) return QStringLiteral("Show all objects");
    if (step == 0)              return QStringLiteral("Auto — by zoom");
    return step > 0 ? QStringLiteral("More objects (+%1)").arg(step)
                    : QStringLiteral("Fewer objects (%1)").arg(step);  // step is negative
}
} // namespace

ChartDetailDialog::ChartDetailDialog(double detailLevel, double scaminLevel,
                                     QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Chart Detail"));
    resize(480, 440);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(12);

    // --- Detail bias (chart-band selection) --------------------------------
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
    updateDetailLabel();
    connect(slider_, &QSlider::valueChanged, this, [this] { updateDetailLabel(); });

    // --- Object detail (SCAMIN declutter) ----------------------------------
    auto* scaminIntro = new QLabel(QStringLiteral(
        "Control how soon individual objects (buoys, beacons, soundings, …) "
        "drop off as you zoom out, using each object's built-in SCAMIN scale. "
        "Left of centre declutters; right of centre keeps more on screen. The "
        "ends hide or show all objects regardless of zoom."));
    scaminIntro->setWordWrap(true);
    col->addWidget(scaminIntro);

    auto* scaminCaption = new QLabel(QStringLiteral("Object detail:"));
    scaminCaption->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(scaminCaption);

    scaminSlider_ = new QSlider(Qt::Horizontal);
    scaminSlider_->setMinimum(kScaminStepMin);
    scaminSlider_->setMaximum(kScaminStepMax);
    scaminSlider_->setSingleStep(1);
    scaminSlider_->setPageStep(1);
    scaminSlider_->setTickPosition(QSlider::TicksBelow);
    scaminSlider_->setTickInterval(1);
    scaminSlider_->setMinimumHeight(44);
    int sStep = scaminLevelToStep(scaminLevel);
    if (sStep < kScaminStepMin) sStep = kScaminStepMin;
    if (sStep > kScaminStepMax) sStep = kScaminStepMax;
    scaminSlider_->setValue(sStep);
    col->addWidget(scaminSlider_);

    scaminValueLabel_ = new QLabel;
    scaminValueLabel_->setAlignment(Qt::AlignCenter);
    scaminValueLabel_->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    col->addWidget(scaminValueLabel_);
    updateScaminLabel();
    connect(scaminSlider_, &QSlider::valueChanged, this, [this] { updateScaminLabel(); });

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

double ChartDetailDialog::scaminLevel() const {
    return scaminStepToLevel(scaminSlider_->value());
}

void ChartDetailDialog::updateDetailLabel() {
    valueLabel_->setText(formatLevel(stepToLevel(slider_->value())));
}

void ChartDetailDialog::updateScaminLabel() {
    scaminValueLabel_->setText(formatScamin(scaminSlider_->value()));
}
