#include "waypoint_properties_dialog.hpp"
#include "units.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>

WaypointPropertiesDialog::WaypointPropertiesDialog(const Waypoint& wpt, QWidget* parent)
    : QDialog(parent), work_(wpt) {
    setWindowTitle(QStringLiteral("Waypoint Properties"));
    resize(420, 0);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(8);

    auto* form = new QFormLayout;
    nameEdit_ = new QLineEdit(work_.name);
    nameEdit_->setMinimumHeight(36);
    descEdit_ = new QLineEdit(work_.description);
    descEdit_->setMinimumHeight(36);
    // Free text (no validator): coordinate formats like DMS aren't plain numbers;
    // parsed tolerantly on OK via units::parseLatitude/Longitude.
    latEdit_ = new QLineEdit(units::formatLatitude(work_.lat));
    latEdit_->setMinimumHeight(36);
    lonEdit_ = new QLineEdit(units::formatLongitude(work_.lon));
    lonEdit_->setMinimumHeight(36);
    form->addRow(QStringLiteral("Name"), nameEdit_);
    form->addRow(QStringLiteral("Description"), descEdit_);
    form->addRow(QStringLiteral("Latitude"), latEdit_);
    form->addRow(QStringLiteral("Longitude"), lonEdit_);
    col->addLayout(form);

    col->addStretch(1);

    auto* row = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    auto* okBtn     = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn}) b->setMinimumHeight(44);
    okBtn->setDefault(true);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(okBtn);
    col->addLayout(row);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &WaypointPropertiesDialog::onOk);

    nameEdit_->setFocus();
    nameEdit_->selectAll();
}

Waypoint WaypointPropertiesDialog::currentWaypoint() const {
    Waypoint w = work_;                 // keeps id / created / symbol / visible
    w.name = nameEdit_->text().trimmed();
    w.description = descEdit_->text().trimmed();
    bool ok = false;
    const double la = units::parseLatitude(latEdit_->text(), &ok);
    if (ok) w.lat = la;
    const double lo = units::parseLongitude(lonEdit_->text(), &ok);
    if (ok) w.lon = lo;
    return w;
}

void WaypointPropertiesDialog::onOk() {
    bool okLat = false, okLon = false;
    units::parseLatitude(latEdit_->text(), &okLat);
    units::parseLongitude(lonEdit_->text(), &okLon);
    if (!okLat || !okLon) {
        QMessageBox::information(this, QStringLiteral("Waypoint Properties"),
            QStringLiteral("Please enter a valid latitude and longitude."));
        return;
    }
    accept();
}
