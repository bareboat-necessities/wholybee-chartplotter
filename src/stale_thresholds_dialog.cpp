#include "stale_thresholds_dialog.hpp"
#include "touch_spin_box.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    cap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    cap->setWordWrap(true);
    col->addWidget(cap);
    col->addWidget(box);
    return w;
}
} // namespace

StaleThresholdsDialog::StaleThresholdsDialog(double staleSeconds, double invalidSeconds,
                                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Stale Data Thresholds"));
    resize(440, 320);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "How long an ownship fix is trusted before it is flagged as stale, "
        "then hidden."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    staleBox_ = new TouchSpinBox;
    staleBox_->setRange(1.0, 600.0);
    staleBox_->setSingleStep(1.0);
    staleBox_->setDecimals(0);
    staleBox_->setSuffix(QStringLiteral(" s"));
    staleBox_->setValue(staleSeconds);
    col->addWidget(labelledStepper(
        QStringLiteral("Mark the fix Stale after:"), staleBox_));

    invalidBox_ = new TouchSpinBox;
    invalidBox_->setRange(staleSeconds + 1.0, 3600.0);
    invalidBox_->setSingleStep(1.0);
    invalidBox_->setDecimals(0);
    invalidBox_->setSuffix(QStringLiteral(" s"));
    invalidBox_->setValue(std::max(invalidSeconds, staleSeconds + 1.0));
    col->addWidget(labelledStepper(
        QStringLiteral("Mark the fix Invalid (hidden) after:"), invalidBox_));

    // Invalid must always exceed Stale: raise its floor as Stale grows.
    connect(staleBox_, &TouchSpinBox::valueChanged, this, [this](double s) {
        invalidBox_->setRange(s + 1.0, 3600.0);
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

double StaleThresholdsDialog::staleSeconds() const   { return staleBox_->value(); }
double StaleThresholdsDialog::invalidSeconds() const { return invalidBox_->value(); }
