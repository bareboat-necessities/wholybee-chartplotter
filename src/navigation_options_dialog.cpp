#include "navigation_options_dialog.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

NavigationOptionsDialog::NavigationOptionsDialog(double arrivalRadiusNm, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Navigation Options"));
    resize(440, 240);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "How close the boat must come to a waypoint, in nautical miles, for it to "
        "count as arrived."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* caption = new QLabel(QStringLiteral("Arrival radius:"));
    caption->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(caption);

    arrivalBox_ = new TouchSpinBox;
    arrivalBox_->setRange(0.001, 10.0);
    arrivalBox_->setDecimals(3);          // precision 0.001 nm
    arrivalBox_->setSingleStep(0.01);
    arrivalBox_->setSuffix(QStringLiteral(" nm"));
    arrivalBox_->setValue(arrivalRadiusNm);
    col->addWidget(arrivalBox_);

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

double NavigationOptionsDialog::arrivalRadiusNm() const { return arrivalBox_->value(); }
