#pragma once
#include <QDialog>

class NavDataStore;
class QTableWidget;
class QTimer;

// Modeless inspector listing the live values currently held in the NavDataStore
// (one row per populated field: Field / Value / Source / Age). Values refresh
// as the store updates. Each value ages independently: while it is Stale its
// row is greyed; once it goes Invalid the row is removed.
class NavDataBrowserWindow : public QDialog {
    Q_OBJECT
public:
    explicit NavDataBrowserWindow(const NavDataStore* store, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    void setCell(int row, int col, const QString& text, bool muted);

    const NavDataStore* store_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;     // ticks the Age row + stale colour
};
