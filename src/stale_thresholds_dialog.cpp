#include "stale_thresholds_dialog.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <algorithm>

namespace {
// A titled value row: a small caption above a touch stepper.
QWidget* labelledStepper(const QString& caption, TouchSpinBox* box) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    auto* cap = new QLabel(caption);
    cap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    cap->setWordWrap(true);
    col->addWidget(cap);
    col->addWidget(box);
    return w;
}

QFrame* makeDivider() {
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    return line;
}
} // namespace

StaleThresholdsDialog::StaleThresholdsDialog(double staleSeconds, double invalidSeconds,
                                             double aisStaleSeconds, double aisLostSeconds,
                                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Stale Data Thresholds"));
    resize(440, 480);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    // ---- Ownship section ----------------------------------------------------
    auto* ownIntro = new QLabel(QStringLiteral(
        "How long an ownship fix is trusted before it is flagged as stale, "
        "then hidden."));
    ownIntro->setWordWrap(true);
    col->addWidget(ownIntro);

    staleBox_ = new TouchSpinBox;
    staleBox_->setRange(1.0, 600.0);
    staleBox_->setSingleStep(1.0);
    staleBox_->setDecimals(0);
    staleBox_->setSuffix(QStringLiteral(" s"));
    staleBox_->setValue(staleSeconds);
    col->addWidget(labelledStepper(
        QStringLiteral("Ownship — mark Stale after:"), staleBox_));

    invalidBox_ = new TouchSpinBox;
    invalidBox_->setRange(staleSeconds + 1.0, 3600.0);
    invalidBox_->setSingleStep(1.0);
    invalidBox_->setDecimals(0);
    invalidBox_->setSuffix(QStringLiteral(" s"));
    invalidBox_->setValue(std::max(invalidSeconds, staleSeconds + 1.0));
    col->addWidget(labelledStepper(
        QStringLiteral("Ownship — mark Invalid (hidden) after:"), invalidBox_));

    connect(staleBox_, &TouchSpinBox::valueChanged, this, [this](double s) {
        invalidBox_->setRange(s + 1.0, 3600.0);
    });

    // ---- AIS section --------------------------------------------------------
    col->addWidget(makeDivider());

    auto* aisIntro = new QLabel(QStringLiteral(
        "How long an AIS target is kept before it is greyed out, then "
        "removed from the display."));
    aisIntro->setWordWrap(true);
    col->addWidget(aisIntro);

    const double aisStaleMin = aisStaleSeconds / 60.0;
    const double aisLostMin  = aisLostSeconds  / 60.0;

    aisStaleBox_ = new TouchSpinBox;
    aisStaleBox_->setRange(1.0, 60.0);
    aisStaleBox_->setSingleStep(1.0);
    aisStaleBox_->setDecimals(0);
    aisStaleBox_->setSuffix(QStringLiteral(" min"));
    aisStaleBox_->setValue(aisStaleMin);
    col->addWidget(labelledStepper(
        QStringLiteral("AIS — mark Stale after:"), aisStaleBox_));

    aisLostBox_ = new TouchSpinBox;
    aisLostBox_->setRange(aisStaleMin + 1.0, 120.0);
    aisLostBox_->setSingleStep(1.0);
    aisLostBox_->setDecimals(0);
    aisLostBox_->setSuffix(QStringLiteral(" min"));
    aisLostBox_->setValue(std::max(aisLostMin, aisStaleMin + 1.0));
    col->addWidget(labelledStepper(
        QStringLiteral("AIS — remove after:"), aisLostBox_));

    connect(aisStaleBox_, &TouchSpinBox::valueChanged, this, [this](double s) {
        aisLostBox_->setRange(s + 1.0, 120.0);
    });

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

double StaleThresholdsDialog::staleSeconds()    const { return staleBox_->value(); }
double StaleThresholdsDialog::invalidSeconds()  const { return invalidBox_->value(); }
double StaleThresholdsDialog::aisStaleSeconds() const { return aisStaleBox_->value() * 60.0; }
double StaleThresholdsDialog::aisLostSeconds()  const { return aisLostBox_->value()  * 60.0; }
