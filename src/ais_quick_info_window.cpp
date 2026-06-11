#include "ais_quick_info_window.hpp"
#include "ais_target_store.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QLabel>

AisQuickInfoWindow::AisQuickInfoWindow(quint32 mmsi, const AisTargetStore* store,
                                       QWidget* parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint), mmsi_(mmsi), store_(store) {
    // Show without grabbing focus so the chart keeps receiving the clicks/pans
    // that dismiss this popup, and delete on close so the caller's QPointer clears.
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "AisQuickInfoWindow { background:%1; border:1px solid %2; border-radius:6px; }")
        .arg(theme::menu().panelBg, theme::menu().panelBorder));

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 8, 12, 8);
    col->setSpacing(2);

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    col->addWidget(titleLabel_);

    cogLabel_ = new QLabel(this);
    sogLabel_ = new QLabel(this);
    for (QLabel* l : {cogLabel_, sogLabel_})
        l->setStyleSheet(QStringLiteral("font-size:13px;"));
    col->addWidget(cogLabel_);
    col->addWidget(sogLabel_);

    if (store_) {
        connect(store_, &AisTargetStore::targetUpdated, this, [this](quint32 m) {
            if (m == mmsi_) refresh();
        });
        // The target aged out — the quick look has nothing left to show.
        connect(store_, &AisTargetStore::targetExpired, this, [this](quint32 m) {
            if (m == mmsi_) close();
        });
    }
    refresh();
}

void AisQuickInfoWindow::refresh() {
    const AisTarget* t = store_ ? store_->target(mmsi_) : nullptr;

    const QString title = (t && !t->name.isEmpty())
        ? t->name
        : QStringLiteral("MMSI %1").arg(mmsi_);
    titleLabel_->setText(title);

    cogLabel_->setText(QStringLiteral("COG: %1").arg(
        (t && t->cogDegTrue)
            ? QString::number(*t->cogDegTrue, 'f', 1) + QStringLiteral("°")
            : QStringLiteral("—")));
    sogLabel_->setText(QStringLiteral("SOG: %1").arg(
        (t && t->sogKnots)
            ? QString::number(*t->sogKnots, 'f', 1) + QStringLiteral(" kn")
            : QStringLiteral("—")));

    adjustSize();
}
