#include "data_priority_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>

namespace { constexpr int kIdRole = Qt::UserRole; }

DataPriorityDialog::DataPriorityDialog(const QList<DataSourceInfo>& orderedSources, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Data Priority"));
    resize(420, 380);

    auto* col = new QVBoxLayout(this);

    auto* intro = new QLabel(QStringLiteral(
        "Order the navigation sources by priority. Data is used from the source "
        "highest in the list; when its data goes invalid, the next source takes "
        "over. Use Up / Down to reorder."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* row = new QHBoxLayout;
    list_ = new QListWidget;
    list_->setStyleSheet(QStringLiteral("QListWidget::item{ padding:12px; }"));
    for (const DataSourceInfo& s : orderedSources) {
        auto* item = new QListWidgetItem(s.name, list_);
        item->setData(kIdRole, s.id);
    }
    if (list_->count() > 0) list_->setCurrentRow(0);
    row->addWidget(list_, 1);

    // Vertical Up/Down stack beside the list, sized for touch.
    auto* btns = new QVBoxLayout;
    auto* upBtn   = new QPushButton(QStringLiteral("▲  Up"));
    auto* downBtn = new QPushButton(QStringLiteral("▼  Down"));
    for (QPushButton* b : {upBtn, downBtn}) b->setMinimumHeight(48);
    btns->addWidget(upBtn);
    btns->addWidget(downBtn);
    btns->addStretch(1);
    row->addLayout(btns);
    col->addLayout(row);

    auto* footer = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    auto* okBtn     = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn}) b->setMinimumHeight(44);
    okBtn->setDefault(true);
    footer->addStretch(1);
    footer->addWidget(cancelBtn);
    footer->addWidget(okBtn);
    col->addLayout(footer);

    connect(upBtn,   &QPushButton::clicked, this, [this] { move(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { move(+1); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
}

void DataPriorityDialog::move(int delta) {
    const int r = list_->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= list_->count()) return;
    QListWidgetItem* item = list_->takeItem(r);
    list_->insertItem(t, item);
    list_->setCurrentRow(t);
}

QStringList DataPriorityDialog::orderedIds() const {
    QStringList ids;
    for (int i = 0; i < list_->count(); ++i)
        ids << list_->item(i)->data(kIdRole).toString();
    return ids;
}
