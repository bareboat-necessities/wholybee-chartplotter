#pragma once
#include <QString>
#include <QList>
#include "chart_loader.hpp"   // FeatureKind

// One S-57 attribute of a picked object: the raw 6-char acronym and its value
// string (as captured during the cell parse).
struct ChartObjectAttr {
    QString key;
    QString value;
};

// A chart object identified by a chart-query click. Carries enough to render a
// detail window without going back to GDAL: the object class, its name, the
// attributes captured for it, and a representative geographic position. Produced
// by ChartView::pickObjects and consumed by MainWindow / ChartObjectInfoWindow.
struct ChartObjectInfo {
    QString     objClass;             // S-57 acronym, e.g. "BOYLAT"
    QString     name;                 // OBJNAM (may be empty)
    FeatureKind kind = FeatureKind::Point;
    bool        hasDepth = false;
    double      depthM = 0.0;         // for soundings / depth contours
    int         scaleMin = 0;         // S-57 SCAMIN (0 = none)
    double      lat = 0.0;
    double      lon = 0.0;
    QList<ChartObjectAttr> attrs;     // captured S-57 attributes (acronym/value)
};
