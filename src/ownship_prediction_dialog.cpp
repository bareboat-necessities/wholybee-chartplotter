#include "ownship_prediction_dialog.hpp"
#include "touch_spin_box.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

OwnshipPredictionDialog::OwnshipPredictionDialog(double minutes, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Ownship Course Prediction"));
    resize(440, 240);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "How far ahead the course-prediction line reaches, as minutes of travel "
        "at the current speed."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* caption = new QLabel(QStringLiteral("Prediction length:"));
    caption->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    col->addWidget(caption);

    minutesBox_ = new TouchSpinBox;
    minutesBox_->setRange(1.0, 120.0);
    minutesBox_->setSingleStep(1.0);
    minutesBox_->setDecimals(0);
    minutesBox_->setSuffix(QStringLiteral(" min"));
    minutesBox_->setValue(minutes);
    col->addWidget(minutesBox_);

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

double OwnshipPredictionDialog::minutes() const { return minutesBox_->value(); }
