#include "dangerous_ships_dialog.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QFrame>
#include <QPushButton>

namespace {
QFrame* makeDivider() {
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    return line;
}

// A rule row: an enable checkbox with a touch stepper indented beneath it. The
// stepper is greyed out while the checkbox is unchecked. Returns the assembled
// widget; the caller keeps the checkbox/box pointers for read-back.
QWidget* ruleRow(QCheckBox* check, TouchSpinBox* box) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    check->setStyleSheet(QStringLiteral("font-size:14px;"));
    col->addWidget(check);

    // Indent the stepper so it reads as belonging to the checkbox above it.
    auto* indent = new QHBoxLayout;
    indent->setContentsMargins(24, 0, 0, 0);
    indent->addWidget(box);
    col->addLayout(indent);

    box->setEnabled(check->isChecked());
    QObject::connect(check, &QCheckBox::toggled, box, &TouchSpinBox::setEnabled);
    return w;
}
} // namespace

DangerousShipsDialog::DangerousShipsDialog(bool ignoreFarEnabled, double ignoreFarNm,
                                           bool cpaEnabled, double cpaNm,
                                           bool tcpaEnabled, double tcpaMin,
                                           QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Dangerous Ships"));
    resize(440, 460);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    // ---- Pre-filter ---------------------------------------------------------
    ignoreFarCheck_ = new QCheckBox(
        QStringLiteral("Ignore CPA for targets greater than (nm)"));
    ignoreFarCheck_->setChecked(ignoreFarEnabled);
    ignoreFarBox_ = new TouchSpinBox;
    ignoreFarBox_->setRange(0.1, 100.0);
    ignoreFarBox_->setSingleStep(1.0);
    ignoreFarBox_->setDecimals(1);
    ignoreFarBox_->setSuffix(QStringLiteral(" nm"));
    ignoreFarBox_->setValue(ignoreFarNm);
    col->addWidget(ruleRow(ignoreFarCheck_, ignoreFarBox_));

    col->addWidget(makeDivider());

    // ---- Dangerous-if rules -------------------------------------------------
    auto* heading = new QLabel(QStringLiteral("A ship is dangerous if:"));
    heading->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(heading);

    cpaCheck_ = new QCheckBox(QStringLiteral("CPA is less than (nm)"));
    cpaCheck_->setChecked(cpaEnabled);
    cpaBox_ = new TouchSpinBox;
    cpaBox_->setRange(0.1, 50.0);
    cpaBox_->setSingleStep(0.1);
    cpaBox_->setDecimals(1);
    cpaBox_->setSuffix(QStringLiteral(" nm"));
    cpaBox_->setValue(cpaNm);
    col->addWidget(ruleRow(cpaCheck_, cpaBox_));

    tcpaCheck_ = new QCheckBox(QStringLiteral("…and TCPA is less than (min)"));
    tcpaCheck_->setChecked(tcpaEnabled);
    tcpaBox_ = new TouchSpinBox;
    tcpaBox_->setRange(1.0, 120.0);
    tcpaBox_->setSingleStep(1.0);
    tcpaBox_->setDecimals(0);
    tcpaBox_->setSuffix(QStringLiteral(" min"));
    tcpaBox_->setValue(tcpaMin);
    col->addWidget(ruleRow(tcpaCheck_, tcpaBox_));

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

bool   DangerousShipsDialog::ignoreFarEnabled() const { return ignoreFarCheck_->isChecked(); }
double DangerousShipsDialog::ignoreFarNm()      const { return ignoreFarBox_->value(); }
bool   DangerousShipsDialog::cpaEnabled()  const { return cpaCheck_->isChecked(); }
double DangerousShipsDialog::cpaNm()       const { return cpaBox_->value(); }
bool   DangerousShipsDialog::tcpaEnabled() const { return tcpaCheck_->isChecked(); }
double DangerousShipsDialog::tcpaMin()     const { return tcpaBox_->value(); }
