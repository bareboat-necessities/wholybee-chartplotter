#include "ownship_mmsi_dialog.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>

OwnshipMmsiDialog::OwnshipMmsiDialog(const QString& currentMmsi, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Own Ship MMSI"));
    resize(400, 240);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Enter the 9-digit MMSI of your vessel. Leave blank to clear."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* caption = new QLabel(QStringLiteral("MMSI (9 digits):"));
    caption->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(caption);

    edit_ = new QLineEdit;
    edit_->setPlaceholderText(QStringLiteral("e.g. 123456789"));
    edit_->setMaxLength(9);
    edit_->setMinimumHeight(44);
    edit_->setStyleSheet(QStringLiteral("font-size:16px; padding:4px 8px;"));
    // Allow only digits; length enforced by maxLength + updateOkState.
    edit_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,9}")), edit_));
    edit_->setText(currentMmsi);
    col->addWidget(edit_);

    col->addStretch(1);

    auto* row = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    okBtn_ = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn_}) b->setMinimumHeight(44);
    okBtn_->setDefault(true);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(okBtn_);
    col->addLayout(row);

    updateOkState();
    connect(edit_, &QLineEdit::textChanged, this, [this] { updateOkState(); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn_,    &QPushButton::clicked, this, &QDialog::accept);
}

QString OwnshipMmsiDialog::mmsi() const {
    return edit_->text().trimmed();
}

void OwnshipMmsiDialog::updateOkState() {
    const QString t = edit_->text().trimmed();
    okBtn_->setEnabled(t.isEmpty() || t.length() == 9);
}
