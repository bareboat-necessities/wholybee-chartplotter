#pragma once
#include <QDialog>
#include <QVector>
#include "settings.hpp"   // ChartSet

class QListWidget;

// Touch-friendly manager for the user's named chart directories.
//
// Edits a working copy of the set list; the caller reads chartSets() after the
// dialog is accepted and persists the result through Settings. The dialog never
// touches Settings directly.
class ChartSetsDialog : public QDialog {
    Q_OBJECT
public:
    ChartSetsDialog(const QVector<ChartSet>& sets, QWidget* parent = nullptr);
    QVector<ChartSet> chartSets() const { return sets_; }

private slots:
    void addSet();
    void removeSelected();

private:
    void refreshList();

    QVector<ChartSet> sets_;
    QListWidget* list_ = nullptr;
};
