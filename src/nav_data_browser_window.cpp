#include "nav_data_browser_window.hpp"
#include "nav_data_store.hpp"

#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QColor>
#include <vector>
#include <utility>

NavDataBrowserWindow::NavDataBrowserWindow(const NavDataStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("NavData Browser"));
    resize(420, 460);
    setWindowFlag(Qt::Window, true);   // real top-level window, modeless via show()

    auto* col = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    col->addWidget(table_);

    if (store_) {
        connect(store_, &NavDataStore::ownshipChanged,   this, &NavDataBrowserWindow::refresh);
        connect(store_, &NavDataStore::freshnessChanged, this, [this](NavFreshness) { refresh(); });
    }
    // Tick so the Age value keeps counting and the stale colour appears on time
    // even when no new data arrives.
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

    const NavFreshness f = store_->freshness();
    if (f == NavFreshness::Invalid) {
        table_->setRowCount(0);    // invalid -> all rows removed
        return;
    }
    const QColor color = (f == NavFreshness::Stale) ? QColor(150, 150, 150)   // grey
                                                    : QColor(20, 20, 20);     // ~black
    const OwnshipState& s = store_->ownship();

    auto ang = [](double v) { return QString::number(v, 'f', 1) + QStringLiteral("°"); };

    std::vector<std::pair<QString, QString>> rows;
    if (s.latitudeDeg)
        rows.emplace_back(QStringLiteral("Latitude"),  QString::number(*s.latitudeDeg,  'f', 6) + QStringLiteral("°"));
    if (s.longitudeDeg)
        rows.emplace_back(QStringLiteral("Longitude"), QString::number(*s.longitudeDeg, 'f', 6) + QStringLiteral("°"));
    if (s.cogDegTrue)     rows.emplace_back(QStringLiteral("COG (true)"),     ang(*s.cogDegTrue));
    if (s.sogKnots)       rows.emplace_back(QStringLiteral("SOG"),            QString::number(*s.sogKnots, 'f', 1) + QStringLiteral(" kn"));
    if (s.headingDegTrue) rows.emplace_back(QStringLiteral("Heading (true)"), ang(*s.headingDegTrue));
    if (s.headingDegMag)  rows.emplace_back(QStringLiteral("Heading (mag)"),  ang(*s.headingDegMag));
    if (s.variationDeg)   rows.emplace_back(QStringLiteral("Variation"),      ang(*s.variationDeg));
    if (s.depthMeters)    rows.emplace_back(QStringLiteral("Depth"),          QString::number(*s.depthMeters, 'f', 1) + QStringLiteral(" m"));
    if (s.windSpeedKnots) rows.emplace_back(QStringLiteral("Wind speed"),     QString::number(*s.windSpeedKnots, 'f', 1) + QStringLiteral(" kn"));
    if (s.windAngleDeg)   rows.emplace_back(QStringLiteral("Wind angle"),     ang(*s.windAngleDeg));
    if (!s.meta.source.isEmpty())
        rows.emplace_back(QStringLiteral("Source"), s.meta.source);
    rows.emplace_back(QStringLiteral("Age"), QString::number(s.meta.ageSeconds, 'f', 1) + QStringLiteral(" s"));

    // Update in place (only resize when the field set changes) to avoid flicker.
    if (table_->rowCount() != int(rows.size()))
        table_->setRowCount(int(rows.size()));
    for (int i = 0; i < int(rows.size()); ++i) {
        setCell(i, 0, rows[i].first,  color);
        setCell(i, 1, rows[i].second, color);
    }
}
