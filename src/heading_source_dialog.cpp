#include "heading_source_dialog.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>

HeadingSourceDialog::HeadingSourceDialog(HeadingSource current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Heading Source"));
    resize(400, 260);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Choose what the ownship symbol points along. If the chosen source has "
        "no data, the other is used as a fallback."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    headingRadio_ = new QRadioButton(QStringLiteral("Heading (where the bow points)"));
    cogRadio_     = new QRadioButton(QStringLiteral("COG (course over ground)"));
    for (QRadioButton* r : {headingRadio_, cogRadio_}) {
        r->setMinimumHeight(40);
        r->setStyleSheet(QStringLiteral("font-size:15px;"));
        col->addWidget(r);
    }
    // Mutually exclusive selection.
    auto* group = new QButtonGroup(this);
    group->addButton(headingRadio_);
    group->addButton(cogRadio_);

    (current == HeadingSource::Cog ? cogRadio_ : headingRadio_)->setChecked(true);

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

HeadingSource HeadingSourceDialog::source() const {
    return cogRadio_->isChecked() ? HeadingSource::Cog : HeadingSource::Heading;
}
