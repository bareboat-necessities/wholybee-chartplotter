#pragma once
#include <QDialog>
#include <QStringList>

class QListWidget;

// Reorder the navigation data sources by priority. Highest priority is at the
// top; data is taken from the highest-priority source and falls back to the
// next when that source's data goes invalid. Edits a working copy; the caller
// reads orderedIds() after acceptance and persists through Settings.
class DataPriorityDialog : public QDialog {
    Q_OBJECT
public:
    DataPriorityDialog(const QStringList& orderedSourceIds, QWidget* parent = nullptr);

    QStringList orderedIds() const;

private:
    void move(int delta);   // move the selected row by delta (-1 up, +1 down)

    QListWidget* list_ = nullptr;
};
