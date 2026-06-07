#include "units_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>

namespace {
// A radio button tall enough for touch, carrying its enum value as the button id.
QRadioButton* addChoice(QButtonGroup* group, QVBoxLayout* into,
                        const QString& text, int id, bool checked) {
    auto* rb = new QRadioButton(text);
    rb->setMinimumHeight(40);
    rb->setStyleSheet(QStringLiteral("QRadioButton{ font-size:15px; }"));
    rb->setChecked(checked);
    group->addButton(rb, id);
    into->addWidget(rb);
    return rb;
}
} // namespace

UnitsDialog::UnitsDialog(DepthUnit depth, DistanceUnit distance, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Units"));
    resize(420, 360);

    auto* col = new QVBoxLayout(this);

    // ---- Depth ----
    auto* depthBox = new QGroupBox(QStringLiteral("Depth (Soundings)"));
    auto* depthCol = new QVBoxLayout(depthBox);
    depthGroup_ = new QButtonGroup(this);
    addChoice(depthGroup_, depthCol, units::depthUnitLabel(DepthUnit::Feet),
              int(DepthUnit::Feet),   depth == DepthUnit::Feet);
    addChoice(depthGroup_, depthCol, units::depthUnitLabel(DepthUnit::Meters),
              int(DepthUnit::Meters), depth == DepthUnit::Meters);
    col->addWidget(depthBox);

    // ---- Distance ----
    auto* distBox = new QGroupBox(QStringLiteral("Distance"));
    auto* distCol = new QVBoxLayout(distBox);
    distGroup_ = new QButtonGroup(this);
    addChoice(distGroup_, distCol, units::distanceUnitLabel(DistanceUnit::NauticalMiles),
              int(DistanceUnit::NauticalMiles), distance == DistanceUnit::NauticalMiles);
    addChoice(distGroup_, distCol, units::distanceUnitLabel(DistanceUnit::StatuteMiles),
              int(DistanceUnit::StatuteMiles),  distance == DistanceUnit::StatuteMiles);
    addChoice(distGroup_, distCol, units::distanceUnitLabel(DistanceUnit::Kilometers),
              int(DistanceUnit::Kilometers),    distance == DistanceUnit::Kilometers);
    col->addWidget(distBox);

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
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
}

DepthUnit UnitsDialog::depthUnit() const {
    return DepthUnit(depthGroup_->checkedId());
}

DistanceUnit UnitsDialog::distanceUnit() const {
    return DistanceUnit(distGroup_->checkedId());
}
