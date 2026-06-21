#pragma once
#include "frameless_info_dialog.hpp"
#include "chart_object.hpp"
#include "units.hpp"   // DepthUnit
#include <QString>
#include <QVector>

class QGridLayout;
class QWidget;

// A friendly, human-readable name for an S-57 object-class acronym (e.g.
// "BOYLAT" -> "Lateral buoy"). Falls back to the acronym itself when unknown.
// Shared with the multi-object chooser so both show the same labels.
QString chartObjectClassName(const QString& acronym);

// Modeless detail window for a single chart object, opened by clicking it on the
// chart. Shows the object class, its name, depth (where applicable), position,
// and the captured S-57 attributes. Static snapshot — charts don't change under
// it — so it simply displays the ChartObjectInfo it was given.
//
// Shares the dark, frameless "instrument panel" look (and drag / close-button
// behaviour) of the AIS target window via FramelessInfoDialog, laid out as a
// header plus compact two-column detail grids rather than a scrolling list.
class ChartObjectInfoWindow : public FramelessInfoDialog {
    Q_OBJECT
public:
    ChartObjectInfoWindow(const ChartObjectInfo& obj, DepthUnit depthUnit,
                          QWidget* parent = nullptr);

private:
    // A caption/value pair; `wide` ones take a full row, the rest pack two per row.
    struct Detail { QString caption, value; bool wide = false; };

    // Packs entries into a 4-column grid (caption,value,caption,value): normal
    // pairs two per row, wide ones spanning the full width.
    static void fillGrid(QGridLayout* grid, QWidget* parent,
                         const QVector<Detail>& entries);
};
