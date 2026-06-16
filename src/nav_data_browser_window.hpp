#pragma once
#include <QDialog>

class NavDataStore;
class QTableWidget;
class QTimer;

// Modeless inspector listing the live values currently held in the NavDataStore.
//
// Two tables: the ownship sensor values (one row per populated field:
// Field / Value / Source / Age, each value ageing independently — greyed while
// Stale, removed once Invalid), and below it the computed route-navigation
// (APB / RMB) values, shown only while a route is being navigated.
class NavDataBrowserWindow : public QDialog {
    Q_OBJECT
public:
    explicit NavDataBrowserWindow(const NavDataStore* store, QWidget* parent = nullptr);

private slots:
    void refresh();

private:
    void setCell(QTableWidget* table, int row, int col, const QString& text, bool muted);
    void refreshNavigation();

    const NavDataStore* store_ = nullptr;
    QTableWidget* table_ = nullptr;     // ownship sensor values
    QTableWidget* navTable_ = nullptr;  // route navigation (APB/RMB) values
    QTimer* timer_ = nullptr;     // ticks the Age row + stale colour
};
