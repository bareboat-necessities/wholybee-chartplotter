#include "name_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

NameDialog::NameDialog(const QString& title, const QString& initialName,
                       const QString& initialDescription, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    resize(380, 0);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(8);

    col->addWidget(new QLabel(QStringLiteral("Name")));
    nameEdit_ = new QLineEdit(initialName);
    nameEdit_->setMinimumHeight(40);
    nameEdit_->setClearButtonEnabled(true);
    col->addWidget(nameEdit_);

    col->addWidget(new QLabel(QStringLiteral("Description (optional)")));
    descEdit_ = new QLineEdit(initialDescription);
    descEdit_->setMinimumHeight(40);
    col->addWidget(descEdit_);

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

    nameEdit_->setFocus();
    nameEdit_->selectAll();
}

QString NameDialog::name() const        { return nameEdit_->text().trimmed(); }
QString NameDialog::description() const  { return descEdit_->text().trimmed(); }
