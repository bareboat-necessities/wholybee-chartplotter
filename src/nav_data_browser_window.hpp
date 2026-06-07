#pragma once
#include <QDialog>

class NavDataStore;
class QTableWidget;
class QTimer;
class QColor;

// Modeless inspector listing the live values currently held in the NavDataStore
// (one row per populated field). Values refresh as the store updates. While the
// fix is Stale the text is greyed; once it goes Invalid the rows are removed.
//
// Note: the store tracks a single freshness for the whole ownship fix (driven
// by the position timestamp), so all rows share the same fresh/stale/invalid
// state — per-value freshness would require per-value meta in the store.
class NavDataBrowserWindow : public QDialog {
    Q_OBJECT
public:
    explicit NavDataBrowserWindow(const NavDataStore* store, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    void setCell(int row, int col, const QString& text, const QColor& color);

    const NavDataStore* store_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTimer* timer_ = nullptr;     // ticks the Age row + stale colour
};
