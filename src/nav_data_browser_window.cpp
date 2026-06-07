#include "nav_data_browser_window.hpp"
#include "nav_data_store.hpp"

#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QColor>
#include <QDateTime>
#include <functional>
#include <vector>

NavDataBrowserWindow::NavDataBrowserWindow(const NavDataStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("NavData Browser"));
    resize(520, 460);
    setWindowFlag(Qt::Window, true);   // real top-level window, modeless via show()

    auto* col = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Field"), QStringLiteral("Value"),
         QStringLiteral("Source"), QStringLiteral("Age")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    col->addWidget(table_);

    if (store_)
        connect(store_, &NavDataStore::ownshipChanged, this, &NavDataBrowserWindow::refresh);
    // Tick so Age keeps counting even when no new data or transition arrives.
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &NavDataBrowserWindow::refresh);
    timer_->start();

    refresh();
}

void NavDataBrowserWindow::setCell(int row, int col, const QString& text, const QColor& color) {
    QTableWidgetItem* it = table_->item(row, col);
    if (!it) {
        it = new QTableWidgetItem;
        table_->setItem(row, col, it);
    }
    it->setText(text);
    it->setForeground(color);
}

void NavDataBrowserWindow::refresh() {
    if (!store_) return;
    const OwnshipState& s = store_->ownship();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    struct Row { QString name, value, source, age; bool stale; };
    std::vector<Row> rows;

    auto add = [&](const QString& name, const NavValue& v,
                   const std::function<QString(double)>& fmt) {
        if (!v.valid()) return;   // never set or aged to Invalid -> not listed
        const double age = v.timestampUtc.isValid()
                             ? v.timestampUtc.msecsTo(now) / 1000.0 : 0.0;
        rows.push_back({ name, fmt(v.value), v.source,
                         QString::number(age, 'f', 1) + QStringLiteral(" s"),
                         v.stale() });
    };
    auto angle = [](double v) { return QString::number(v, 'f', 1) + QStringLiteral("°"); };
    auto coord = [](double v) { return QString::number(v, 'f', 6) + QStringLiteral("°"); };
    auto knots = [](double v) { return QString::number(v, 'f', 1) + QStringLiteral(" kn"); };
    auto metres = [](double v) { return QString::number(v, 'f', 1) + QStringLiteral(" m"); };

    add(QStringLiteral("Latitude"),         s.latitudeDeg,            coord);
    add(QStringLiteral("Longitude"),        s.longitudeDeg,           coord);
    add(QStringLiteral("COG (true)"),       s.cogDegTrue,             angle);
    add(QStringLiteral("SOG"),              s.sogKnots,               knots);
    add(QStringLiteral("Water speed"),      s.waterSpeedKnots,        knots);
    add(QStringLiteral("Heading (true)"),   s.headingDegTrue,         angle);
    add(QStringLiteral("Heading (mag)"),    s.headingDegMag,          angle);
    add(QStringLiteral("Variation"),        s.variationDeg,           angle);
    add(QStringLiteral("Depth"),            s.depthMeters,            metres);
    add(QStringLiteral("App. wind angle"),  s.apparentWindAngleDeg,   angle);
    add(QStringLiteral("App. wind speed"),  s.apparentWindSpeedKnots, knots);
    add(QStringLiteral("True wind angle"),  s.trueWindAngleDeg,       angle);
    add(QStringLiteral("True wind speed"),  s.trueWindSpeedKnots,     knots);
    add(QStringLiteral("True wind dir"),    s.trueWindDirectionDeg,   angle);

    // Update in place (resize only when the field set changes) to avoid flicker.
    if (table_->rowCount() != int(rows.size()))
        table_->setRowCount(int(rows.size()));
    for (int i = 0; i < int(rows.size()); ++i) {
        const QColor color = rows[i].stale ? QColor(150, 150, 150)   // grey
                                           : QColor(20, 20, 20);     // ~black
        setCell(i, 0, rows[i].name,   color);
        setCell(i, 1, rows[i].value,  color);
        setCell(i, 2, rows[i].source, color);
        setCell(i, 3, rows[i].age,    color);
    }
}
