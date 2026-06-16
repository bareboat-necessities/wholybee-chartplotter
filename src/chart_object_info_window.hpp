#pragma once
#include <QDialog>
#include "chart_object.hpp"
#include "units.hpp"   // DepthUnit

class QVBoxLayout;

// A friendly, human-readable name for an S-57 object-class acronym (e.g.
// "BOYLAT" -> "Lateral buoy"). Falls back to the acronym itself when unknown.
// Shared with the multi-object chooser so both show the same labels.
QString chartObjectClassName(const QString& acronym);

// Modeless detail window for a single chart object, opened by clicking it on the
// chart. Shows the object class, its name, depth (where applicable), position,
// and the captured S-57 attributes. Static snapshot — charts don't change under
// it — so it simply displays the ChartObjectInfo it was given.
class ChartObjectInfoWindow : public QDialog {
    Q_OBJECT
public:
    ChartObjectInfoWindow(const ChartObjectInfo& obj, DepthUnit depthUnit,
                          QWidget* parent = nullptr);

private:
    void addRow(QVBoxLayout* col, const QString& field, const QString& value);
};
