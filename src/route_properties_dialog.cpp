#include "route_properties_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QDoubleValidator>
#include <QLocale>
#include <QMessageBox>

namespace {
QLineEdit* makeCoordEdit(double value, double lo, double hi) {
    auto* e = new QLineEdit;
    e->setMinimumHeight(34);
    auto* v = new QDoubleValidator(lo, hi, 6, e);
    v->setNotation(QDoubleValidator::StandardNotation);
    v->setLocale(QLocale::c());   // '.' decimal, matching QString::number/toDouble
    e->setValidator(v);
    e->setText(QString::number(value, 'f', 6));
    return e;
}
}  // namespace

RoutePropertiesDialog::RoutePropertiesDialog(const Route& route, QWidget* parent)
    : QDialog(parent), work_(route) {
    setWindowTitle(QStringLiteral("Route Properties"));
    resize(520, 620);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(8);

    auto* form = new QFormLayout;
    nameEdit_ = new QLineEdit(work_.name);
    nameEdit_->setMinimumHeight(36);
    descEdit_ = new QLineEdit(work_.description);
    descEdit_->setMinimumHeight(36);
    form->addRow(QStringLiteral("Name"), nameEdit_);
    form->addRow(QStringLiteral("Description"), descEdit_);
    col->addLayout(form);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet(QStringLiteral("font-size:13px; font-weight:600; padding:4px 2px;"));
    col->addWidget(countLabel_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidgetResizable(true);
    rowContainer_ = new QWidget;
    rowLayout_ = new QVBoxLayout(rowContainer_);
    rowLayout_->setContentsMargins(0, 0, 0, 0);
    rowLayout_->setSpacing(0);
    rowLayout_->addStretch(1);
    scrollArea_->setWidget(rowContainer_);
    // Drag-to-scroll, consistent with the list dialogs. Taps still reach the
    // per-row fields and buttons.
    QScroller::grabGesture(scrollArea_->viewport(), QScroller::LeftMouseButtonGesture);
    col->addWidget(scrollArea_, 1);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    auto* okBtn     = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn}) b->setMinimumHeight(44);
    okBtn->setDefault(true);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    col->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &RoutePropertiesDialog::onOk);

    rebuildRows();
}

void RoutePropertiesDialog::rebuildRows() {
    // Tear down existing rows (cheap: point counts are modest, not on a timer).
    for (Row& r : rows_) r.widget->deleteLater();
    rows_.clear();

    countLabel_->setText(QStringLiteral("Points (%1)").arg(work_.points.size()));

    for (int i = 0; i < work_.points.size(); ++i) {
        const RoutePoint& p = work_.points[i];
        auto* w = new QWidget(rowContainer_);
        w->setStyleSheet(QStringLiteral("QWidget{ border-bottom:1px solid palette(mid); }"));
        auto* hl = new QHBoxLayout(w);
        hl->setContentsMargins(4, 4, 4, 4);
        hl->setSpacing(6);

        auto* num = new QLabel(QString::number(i + 1), w);
        num->setFixedWidth(28);
        num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        num->setStyleSheet(QStringLiteral("border:none; font-weight:600;"));
        hl->addWidget(num);

        Row row;
        row.widget = w;
        row.lat = makeCoordEdit(p.lat, -90.0, 90.0);
        row.lon = makeCoordEdit(p.lon, -180.0, 180.0);
        row.lat->setToolTip(QStringLiteral("Latitude (°)"));
        row.lon->setToolTip(QStringLiteral("Longitude (°)"));
        hl->addWidget(row.lat, 1);
        hl->addWidget(row.lon, 1);

        auto* editBtn = new QPushButton(QStringLiteral("Edit"), w);
        editBtn->setMinimumHeight(34);
        editBtn->setToolTip(QStringLiteral("Drag this point on the chart"));
        connect(editBtn, &QPushButton::clicked, this, [this, i] { emit editPointRequested(i); });
        hl->addWidget(editBtn);

        auto* delBtn = new QPushButton(QStringLiteral("Delete"), w);
        delBtn->setMinimumHeight(34);
        connect(delBtn, &QPushButton::clicked, this, [this, i] { onDeletePoint(i); });
        hl->addWidget(delBtn);

        rowLayout_->insertWidget(rowLayout_->count() - 1, w);   // before the stretch
        rows_.push_back(row);
    }
}

void RoutePropertiesDialog::commitFieldsToWorking() {
    work_.name = nameEdit_->text().trimmed();
    work_.description = descEdit_->text().trimmed();
    for (int i = 0; i < int(rows_.size()) && i < work_.points.size(); ++i) {
        work_.points[i].lat = rows_[i].lat->text().toDouble();
        work_.points[i].lon = rows_[i].lon->text().toDouble();
    }
}

void RoutePropertiesDialog::onDeletePoint(int index) {
    if (index < 0 || index >= work_.points.size()) return;
    commitFieldsToWorking();          // preserve other edits before rebuilding
    work_.points.remove(index);
    rebuildRows();
}

Route RoutePropertiesDialog::currentRoute() const {
    Route r = work_;                  // keeps id / created / visible
    r.name = nameEdit_->text().trimmed();
    r.description = descEdit_->text().trimmed();
    for (int i = 0; i < int(rows_.size()) && i < r.points.size(); ++i) {
        r.points[i].lat = rows_[i].lat->text().toDouble();
        r.points[i].lon = rows_[i].lon->text().toDouble();
    }
    return r;
}

void RoutePropertiesDialog::setRoute(const Route& route) {
    work_ = route;
    nameEdit_->setText(work_.name);
    descEdit_->setText(work_.description);
    rebuildRows();
}

void RoutePropertiesDialog::onOk() {
    commitFieldsToWorking();
    if (work_.points.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Route Properties"),
            QStringLiteral("A route needs at least two points."));
        return;
    }
    for (int i = 0; i < work_.points.size(); ++i) {
        const RoutePoint& p = work_.points[i];
        if (p.lat < -90.0 || p.lat > 90.0 || p.lon < -180.0 || p.lon > 180.0
            || rows_[i].lat->text().trimmed().isEmpty()
            || rows_[i].lon->text().trimmed().isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Route Properties"),
                QStringLiteral("Point %1 has an invalid latitude/longitude.").arg(i + 1));
            return;
        }
    }
    accept();
}
