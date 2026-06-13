#include "route_list_dialog.hpp"
#include "route_store.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>

namespace {
QLabel* makeCell(int fixedWidth, Qt::Alignment align) {
    auto* l = new QLabel;
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setAlignment(align);
    l->setStyleSheet(QStringLiteral("font-size:14px; padding:0 4px; border:none;"));
    if (fixedWidth > 0) l->setFixedWidth(fixedWidth);
    return l;
}
}  // namespace

RouteListDialog::RouteListDialog(RouteStore* store, bool pickMode, QWidget* parent)
    : QDialog(parent), store_(store), pickMode_(pickMode) {
    setWindowTitle(pickMode ? QStringLiteral("Select Route")
                            : QStringLiteral("Routes"));
    resize(520, 600);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(0);
    col->setContentsMargins(0, 0, 0, 8);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("font-size:13px; padding:6px 8px;"));
    col->addWidget(countLabel_);

    {   // static column header
        auto* hdr = new QWidget(this);
        hdr->setStyleSheet(QStringLiteral(
            "background: palette(button); font-size:12px; font-weight:600;"));
        auto* hl = new QHBoxLayout(hdr);
        hl->setContentsMargins(8, 4, 8, 4);
        hl->setSpacing(0);
        auto* hName = new QLabel(QStringLiteral("Name"), hdr);
        hName->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* hPts = new QLabel(QStringLiteral("Points"), hdr);
        hPts->setFixedWidth(70);
        hPts->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* hVis = new QLabel(QStringLiteral("Visible"), hdr);
        hVis->setFixedWidth(64);
        hVis->setAlignment(Qt::AlignCenter);
        hl->addWidget(hName);
        hl->addWidget(hPts);
        hl->addWidget(hVis);
        col->addWidget(hdr);
    }

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(true);
    rowContainer_ = new QWidget;
    rowLayout_ = new QVBoxLayout(rowContainer_);
    rowLayout_->setContentsMargins(0, 0, 0, 0);
    rowLayout_->setSpacing(0);
    rowLayout_->addStretch(1);
    scrollArea_->setWidget(rowContainer_);
    QScroller::grabGesture(scrollArea_->viewport(), QScroller::LeftMouseButtonGesture);
    col->addWidget(scrollArea_, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(8, 8, 8, 0);
    deleteBtn_ = new QPushButton(QStringLiteral("Delete"));
    deleteBtn_->setMinimumHeight(44);
    deleteBtn_->setEnabled(false);
    deleteBtn_->setVisible(!pickMode_);
    connect(deleteBtn_, &QPushButton::clicked, this, &RouteListDialog::deleteSelected);
    btnRow->addWidget(deleteBtn_);
    propsBtn_ = new QPushButton(QStringLiteral("Properties"));
    propsBtn_->setMinimumHeight(44);
    propsBtn_->setEnabled(false);
    propsBtn_->setVisible(!pickMode_);
    connect(propsBtn_, &QPushButton::clicked, this, [this] {
        if (selectedId_ >= 0) emit propertiesRequested(selectedId_);
    });
    btnRow->addWidget(propsBtn_);
    btnRow->addStretch(1);
    auto* closeBtn = new QPushButton(pickMode_ ? QStringLiteral("Cancel")
                                               : QStringLiteral("Close"));
    closeBtn->setMinimumHeight(44);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(closeBtn);
    col->addLayout(btnRow);

    if (store_) connect(store_, &RouteStore::routesChanged, this, &RouteListDialog::refresh);
    refresh();
}

RouteListDialog::Row RouteListDialog::makeRow() {
    Row r;
    r.btn = new QPushButton(rowContainer_);
    r.btn->setFlat(true);
    r.btn->setMinimumHeight(44);
    r.btn->setCheckable(true);
    r.btn->setStyleSheet(QStringLiteral(
        "QPushButton { text-align:left; border:none;"
        " border-bottom:1px solid palette(mid); }"
        "QPushButton:checked { background:palette(highlight);"
        " color:palette(highlighted-text); }"));
    auto* hl = new QHBoxLayout(r.btn);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(0);
    r.name = makeCell(0,  Qt::AlignLeft  | Qt::AlignVCenter);
    r.name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    r.meta = makeCell(70, Qt::AlignRight | Qt::AlignVCenter);
    hl->addWidget(r.name, 1);
    hl->addWidget(r.meta);

    // Visible checkbox: kept clickable (not transparent) so it toggles
    // independently of selecting the row.
    r.vis = new QCheckBox(r.btn);
    r.vis->setFixedWidth(64);
    r.vis->setStyleSheet(QStringLiteral("margin-left:24px;"));
    hl->addWidget(r.vis);

    connect(r.btn, &QPushButton::clicked, this, [this, btn = r.btn] {
        const qint64 id = btn->property("rid").toLongLong();
        if (pickMode_) { emit routePicked(id); accept(); }
        else           { selectRow(id); }
    });
    connect(r.vis, &QCheckBox::toggled, this, [this, box = r.vis](bool on) {
        const qint64 id = box->property("rid").toLongLong();
        if (store_) store_->setRouteVisible(id, on);
    });
    return r;
}

void RouteListDialog::refresh() {
    if (!store_) return;
    const QVector<Route>& routes = store_->routes();

    countLabel_->setText(QStringLiteral("%1 route%2")
                         .arg(routes.size()).arg(routes.size() == 1 ? "" : "s"));

    const int want = routes.size();
    while (int(rows_.size()) < want) {
        Row r = makeRow();
        rowLayout_->insertWidget(rowLayout_->count() - 1, r.btn);
        rows_.push_back(r);
    }
    while (int(rows_.size()) > want) {
        rows_.back().btn->deleteLater();
        rows_.pop_back();
    }

    bool selectionStillExists = false;
    for (int i = 0; i < want; ++i) {
        const Route& rt = routes[i];
        Row& row = rows_[i];
        row.id = rt.id;
        row.btn->setProperty("rid", QVariant::fromValue(rt.id));
        row.vis->setProperty("rid", QVariant::fromValue(rt.id));
        const QString nm = rt.name.isEmpty() ? QStringLiteral("(unnamed)") : rt.name;
        if (row.name->text() != nm) row.name->setText(nm);
        row.meta->setText(QString::number(rt.points.size()));
        // Set checkbox without retriggering the store write.
        row.vis->blockSignals(true);
        row.vis->setChecked(rt.visible);
        row.vis->blockSignals(false);
        if (rt.id == selectedId_) selectionStillExists = true;
    }
    if (!selectionStillExists) selectedId_ = -1;
    restyleRows();
    const bool hasSel = selectedId_ >= 0;
    deleteBtn_->setEnabled(hasSel);
    propsBtn_->setEnabled(hasSel);
}

void RouteListDialog::selectRow(qint64 id) {
    selectedId_ = (selectedId_ == id) ? -1 : id;   // tap again to deselect
    restyleRows();
    const bool hasSel = selectedId_ >= 0;
    deleteBtn_->setEnabled(hasSel);
    propsBtn_->setEnabled(hasSel);
}

void RouteListDialog::restyleRows() {
    for (Row& r : rows_)
        r.btn->setChecked(r.id == selectedId_ && selectedId_ >= 0);
}

void RouteListDialog::deleteSelected() {
    if (!store_ || selectedId_ < 0) return;
    const Route* r = store_->route(selectedId_);
    const QString nm = (r && !r->name.isEmpty()) ? r->name : QStringLiteral("this route");
    if (QMessageBox::question(this, QStringLiteral("Delete Route"),
            QStringLiteral("Delete %1?").arg(nm)) != QMessageBox::Yes)
        return;
    const qint64 id = selectedId_;
    selectedId_ = -1;
    store_->removeRoute(id);   // emits routesChanged -> refresh()
}
