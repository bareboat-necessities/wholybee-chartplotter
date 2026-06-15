#include "nav_display_window.hpp"
#include "nav_data_store.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QEvent>
#include <algorithm>

namespace {
// Right-aligned bold value label.
QLabel* valueLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setStyleSheet(QStringLiteral("font-size:15px; font-weight:600;"));
    return l;
}
// Dim caption in the left column.
QLabel* captionLabel(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("font-size:12px; color: rgba(230,233,238,150);"));
    return l;
}
}  // namespace

NavDisplayWindow::NavDisplayWindow(const NavDataStore* store, QWidget* parent)
    : QFrame(parent), store_(store) {
    setObjectName(QStringLiteral("NavDisplayWindow"));
    setCursor(Qt::OpenHandCursor);
    setStyleSheet(QStringLiteral(
        "#NavDisplayWindow{ background: rgba(30,34,40,235);"
        " border:1px solid rgba(255,255,255,40); border-radius:8px; }"
        "QLabel{ color:#e6e9ee; background:transparent; border:none; }"));

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(12, 8, 12, 10);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);

    destLabel_ = new QLabel(this);
    destLabel_->setStyleSheet(QStringLiteral("font-size:12px; color: rgba(230,233,238,175);"));
    grid->addWidget(destLabel_, 0, 0, 1, 3);

    grid->addWidget(captionLabel(this, QStringLiteral("XTE")), 1, 0);
    xteLabel_ = valueLabel(this);
    grid->addWidget(xteLabel_, 1, 1);
    steerLabel_ = new QLabel(this);
    steerLabel_->setStyleSheet(QStringLiteral("font-size:16px; font-weight:700; color:#ffd24a;"));
    steerLabel_->setAlignment(Qt::AlignCenter);
    grid->addWidget(steerLabel_, 1, 2);

    grid->addWidget(captionLabel(this, QStringLiteral("Bearing")), 2, 0);
    bearingLabel_ = valueLabel(this);
    grid->addWidget(bearingLabel_, 2, 1, 1, 2);

    grid->addWidget(captionLabel(this, QStringLiteral("Range")), 3, 0);
    rangeLabel_ = valueLabel(this);
    grid->addWidget(rangeLabel_, 3, 1, 1, 2);

    grid->addWidget(captionLabel(this, QStringLiteral("VMG")), 4, 0);
    vmgLabel_ = valueLabel(this);
    grid->addWidget(vmgLabel_, 4, 1, 1, 2);

    // Transmit indicator: a small dot (green = APB/RMB/RMC being transmitted on
    // NMEA 0183, red = suppressed by the loop guard) plus a static label.
    txDot_ = new QLabel(this);
    txDot_->setFixedSize(12, 12);
    txLabel_ = new QLabel(QStringLiteral("APB · RMB · RMC"), this);
    txLabel_->setStyleSheet(QStringLiteral("font-size:11px; color: rgba(230,233,238,150);"));
    grid->addWidget(txDot_, 5, 0, Qt::AlignCenter);
    grid->addWidget(txLabel_, 5, 1, 1, 2);

    if (store_)
        connect(store_, &NavDataStore::navigationChanged, this, &NavDisplayWindow::refresh);
    if (parent) parent->installEventFilter(this);

    hide();   // shown only while navigating
    refresh();
}

void NavDisplayWindow::refresh() {
    if (!store_) return;
    const NavigationData& n = store_->navigation();
    if (!n.active) { hide(); return; }

    destLabel_->setText(QStringLiteral("To: %1")
        .arg(n.destinationWaypointId.isEmpty() ? QStringLiteral("—") : n.destinationWaypointId));
    xteLabel_->setText(QString::number(n.xteNm, 'f', 3) + QStringLiteral(" nm"));
    steerLabel_->setText(QString(n.steerDirection));
    bearingLabel_->setText(QString::number(n.bearingPresentToDestDeg, 'f', 1)
                           + QStringLiteral("° ") + QString(n.bearingUnits));
    rangeLabel_->setText(QString::number(n.rangeToDestNm, 'f', 2) + QStringLiteral(" nm"));
    vmgLabel_->setText(QString::number(n.closingVelocityKn, 'f', 1) + QStringLiteral(" kn"));

    // Transmit state: suppressed when the navigation solution itself was sourced
    // from the NMEA 0183 link (loop guard); otherwise it is transmitted.
    const bool suppressed =
        n.source.compare(QLatin1String("nmea0183"), Qt::CaseInsensitive) == 0;
    txDot_->setStyleSheet(QStringLiteral("background:%1; border-radius:6px;")
        .arg(suppressed ? QStringLiteral("#d23b3b") : QStringLiteral("#2e9e44")));
    txDot_->setToolTip(suppressed
        ? QStringLiteral("APB/RMB/RMC transmission suppressed: navigation data came "
                         "from NMEA 0183 (loop guard)")
        : QStringLiteral("Transmitting APB/RMB/RMC on the NMEA 0183 connection "
                         "(when connected)"));

    const bool wasVisible = isVisible();
    adjustSize();
    if (!placed_) { positionDefault(); placed_ = true; }
    show();
    // Only raise on the hidden->shown transition, so a live update never pops the
    // readout above an open side menu (also a child of the view).
    if (!wasVisible) raise();
}

void NavDisplayWindow::positionDefault() {
    if (!parentWidget()) return;
    adjustSize();
    const int x = parentWidget()->width() - width() - 12;
    move(std::max(12, x), 12);
}

void NavDisplayWindow::clampIntoParent() {
    if (!parentWidget()) return;
    const QRect pr = parentWidget()->rect();
    const int x = std::clamp(pos().x(), 0, std::max(0, pr.width()  - width()));
    const int y = std::clamp(pos().y(), 0, std::max(0, pr.height() - height()));
    move(x, y);
}

void NavDisplayWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragOffset_ = e->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
    }
    QFrame::mousePressEvent(e);
}

void NavDisplayWindow::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) {
        move(mapToParent(e->position().toPoint()) - dragOffset_);
        clampIntoParent();
    }
    QFrame::mouseMoveEvent(e);
}

void NavDisplayWindow::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = false;
        setCursor(Qt::OpenHandCursor);
    }
    QFrame::mouseReleaseEvent(e);
}

bool NavDisplayWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize && isVisible())
        clampIntoParent();
    return QFrame::eventFilter(obj, e);
}
