#include "chart_sets_dialog.hpp"

#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QDir>

ChartSetsDialog::ChartSetsDialog(const QVector<ChartSet>& sets, QWidget* parent)
    : QDialog(parent), sets_(sets) {
    setWindowTitle(QStringLiteral("Chart Sets"));
    resize(560, 440);

    auto* col = new QVBoxLayout(this);

    auto* hint = new QLabel(
        QStringLiteral("Define the chart folders you want to switch between."));
    hint->setWordWrap(true);
    col->addWidget(hint);

    list_ = new QListWidget;
    list_->setStyleSheet(QStringLiteral("QListWidget::item{ padding:10px; }"));
    col->addWidget(list_, 1);

    auto* row = new QHBoxLayout;
    auto* addBtn = new QPushButton(QStringLiteral("Add…"));
    auto* rmBtn  = new QPushButton(QStringLiteral("Remove"));
    auto* doneBtn = new QPushButton(QStringLiteral("Done"));
    for (QPushButton* b : {addBtn, rmBtn, doneBtn}) b->setMinimumHeight(44);
    doneBtn->setDefault(true);
    row->addWidget(addBtn);
    row->addWidget(rmBtn);
    row->addStretch(1);
    row->addWidget(doneBtn);
    col->addLayout(row);

    connect(addBtn,  &QPushButton::clicked, this, &ChartSetsDialog::addSet);
    connect(rmBtn,   &QPushButton::clicked, this, &ChartSetsDialog::removeSelected);
    connect(doneBtn, &QPushButton::clicked, this, &QDialog::accept);

    refreshList();
}

void ChartSetsDialog::refreshList() {
    list_->clear();
    for (const ChartSet& cs : sets_) {
        auto* item = new QListWidgetItem(cs.name + QStringLiteral("\n") + cs.directory, list_);
        item->setToolTip(cs.directory);
    }
}

void ChartSetsDialog::addSet() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Chart Folder"));
    if (dir.isEmpty()) return;

    bool ok = false;
    const QString defName = QDir(dir).dirName();
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Chart Set Name"),
        QStringLiteral("Name for this chart set:"),
        QLineEdit::Normal, defName, &ok);
    if (!ok) return;

    ChartSet cs;
    cs.directory = dir;
    cs.name = name.trimmed().isEmpty() ? defName : name.trimmed();
    sets_.push_back(cs);
    refreshList();
    list_->setCurrentRow(sets_.size() - 1);
}

void ChartSetsDialog::removeSelected() {
    const int r = list_->currentRow();
    if (r < 0 || r >= sets_.size()) return;
    sets_.removeAt(r);
    refreshList();
}
