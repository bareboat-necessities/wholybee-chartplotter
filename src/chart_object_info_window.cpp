#include "chart_object_info_window.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QLabel>
#include <QHash>

namespace {

// A compact S-57 object-class dictionary covering the classes a user is most
// likely to click. Unknown acronyms fall through to the acronym itself, so the
// window is still informative for the long tail.
const QHash<QString, QString>& classNames() {
    static const QHash<QString, QString> m = {
        {"BOYLAT", "Lateral buoy"},      {"BOYCAR", "Cardinal buoy"},
        {"BOYISD", "Isolated-danger buoy"}, {"BOYSAW", "Safe-water buoy"},
        {"BOYSPP", "Special-purpose buoy"}, {"BOYINB", "Installation buoy"},
        {"BCNLAT", "Lateral beacon"},    {"BCNCAR", "Cardinal beacon"},
        {"BCNISD", "Isolated-danger beacon"}, {"BCNSAW", "Safe-water beacon"},
        {"BCNSPP", "Special-purpose beacon"},
        {"LIGHTS", "Light"},             {"LITFLT", "Light float"},
        {"LITVES", "Light vessel"},      {"DAYMAR", "Daymark"},
        {"TOPMAR", "Topmark"},           {"RTPBCN", "Radar transponder beacon"},
        {"RADRFL", "Radar reflector"},   {"RETRFL", "Retroreflector"},
        {"FOGSIG", "Fog signal"},
        {"WRECKS", "Wreck"},             {"OBSTRN", "Obstruction"},
        {"UWTROC", "Underwater/awash rock"}, {"ROCKS",  "Rock"},
        {"SOUNDG", "Sounding"},          {"DEPCNT", "Depth contour"},
        {"DEPARE", "Depth area"},        {"DRGARE", "Dredged area"},
        {"COALNE", "Coastline"},         {"SLCONS", "Shoreline construction"},
        {"LNDARE", "Land area"},         {"LNDMRK", "Landmark"},
        {"LNDELV", "Land elevation"},    {"SLOTOP", "Slope topline"},
        {"PILPNT", "Pile / post"},       {"MORFAC", "Mooring facility"},
        {"ACHARE", "Anchorage area"},    {"ACHBRT", "Anchor berth"},
        {"RESARE", "Restricted area"},   {"CTNARE", "Caution area"},
        {"PRCARE", "Precautionary area"},{"TSSLPT", "Traffic-separation lane"},
        {"TSEZNE", "Traffic-separation zone"}, {"FAIRWY", "Fairway"},
        {"NAVLNE", "Navigation line"},   {"RECTRC", "Recommended track"},
        {"DWRTPT", "Deep-water route part"}, {"CBLSUB", "Submarine cable"},
        {"CBLOHD", "Overhead cable"},    {"PIPSOL", "Submarine pipeline"},
        {"BRIDGE", "Bridge"},            {"CAUSWY", "Causeway"},
        {"DAMCON", "Dam"},               {"LOCMAG", "Local magnetic anomaly"},
        {"SBDARE", "Seabed area"},       {"WEDKLP", "Weed / kelp"},
        {"SEAARE", "Sea area / named water"}, {"SPRING", "Spring"},
        {"BERTHS", "Berth"},             {"HRBFAC", "Harbour facility"},
        {"BUISGL", "Building (single)"}, {"BUAARE", "Built-up area"},
        {"PONTON", "Pontoon"},           {"FLODOC", "Floating dock"},
        {"HULKES", "Hulk"},              {"CRANES", "Crane"},
        {"MAGVAR", "Magnetic variation"},
    };
    return m;
}

// Friendly labels for the most common S-57 attribute acronyms. Values are left
// as their raw S-57 codes (decoding every enumerant is a larger undertaking);
// the label at least names each row.
QString attrLabel(const QString& acr) {
    static const QHash<QString, QString> m = {
        {"OBJNAM", "Name"},        {"COLOUR", "Colour"},     {"COLPAT", "Colour pattern"},
        {"BOYSHP", "Buoy shape"},  {"BCNSHP", "Beacon shape"},
        {"CATLAM", "Category (lateral)"}, {"CATCAM", "Category (cardinal)"},
        {"CATSPM", "Category (special)"}, {"CATLIT", "Category of light"},
        {"LITCHR", "Light character"},    {"SIGGRP", "Signal group"},
        {"SIGPER", "Signal period (s)"},  {"VALNMR", "Range (nm)"},
        {"HEIGHT", "Height (m)"},  {"VALSOU", "Sounding (m)"},
        {"DRVAL1", "Depth min (m)"}, {"DRVAL2", "Depth max (m)"},
        {"VERCLR", "Vertical clearance (m)"}, {"VERLEN", "Vertical length (m)"},
        {"WATLEV", "Water level"}, {"CATOBS", "Category of obstruction"},
        {"CATWRK", "Category of wreck"}, {"NATSUR", "Nature of surface"},
        {"INFORM", "Information"},  {"NOBJNM", "Name (national)"},
        {"ORIENT", "Orientation (°)"}, {"PEROND", "Period of day"},
        {"STATUS", "Status"},      {"CONVIS", "Conspicuous"},
    };
    auto it = m.constFind(acr);
    return it != m.constEnd() ? it.value() : acr;
}

} // namespace

QString chartObjectClassName(const QString& acronym) {
    auto it = classNames().constFind(acronym);
    return it != classNames().constEnd() ? it.value() : acronym;
}

ChartObjectInfoWindow::ChartObjectInfoWindow(const ChartObjectInfo& obj,
                                             DepthUnit depthUnit, QWidget* parent)
    : QDialog(parent) {
    const QString cls = chartObjectClassName(obj.objClass);
    setWindowTitle(obj.name.isEmpty() ? cls
                                      : QStringLiteral("%1 — %2").arg(cls, obj.name));
    setWindowFlag(Qt::Window, true);
    resize(360, 420);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(16, 14, 16, 14);
    col->setSpacing(6);

    // Header: friendly class name (+ raw acronym for reference).
    auto* title = new QLabel(cls);
    title->setStyleSheet(QStringLiteral("font-size:18px; font-weight:700;"));
    col->addWidget(title);
    if (obj.objClass != cls) {
        auto* acr = new QLabel(obj.objClass);
        acr->setStyleSheet(QStringLiteral("font-size:12px; color:%1;").arg(theme::textMuted()));
        col->addWidget(acr);
    }
    col->addSpacing(8);

    if (!obj.name.isEmpty()) addRow(col, QStringLiteral("Name"), obj.name);
    if (obj.hasDepth) {
        const double v = (depthUnit == DepthUnit::Meters)
                           ? obj.depthM : obj.depthM * units::kMetersToFeet;
        const QString suffix = (depthUnit == DepthUnit::Meters) ? QStringLiteral(" m")
                                                                : QStringLiteral(" ft");
        addRow(col, QStringLiteral("Depth"),
               QString::number(v, 'f', v < 10.0 ? 1 : 0) + suffix);
    }
    addRow(col, QStringLiteral("Position"),
           units::formatLatitude(obj.lat) + QStringLiteral("  ") +
           units::formatLongitude(obj.lon));
    if (obj.scaleMin > 0)
        addRow(col, QStringLiteral("Min scale"),
               QStringLiteral("1:%1").arg(obj.scaleMin));

    // Remaining captured S-57 attributes (skip OBJNAM — already shown as Name).
    bool anyAttr = false;
    for (const ChartObjectAttr& a : obj.attrs) {
        if (a.key == QStringLiteral("OBJNAM") || a.value.isEmpty()) continue;
        if (!anyAttr) {
            col->addSpacing(8);
            auto* hdr = new QLabel(QStringLiteral("Attributes"));
            hdr->setStyleSheet(QStringLiteral("font-size:12px; color:%1;").arg(theme::textMuted()));
            col->addWidget(hdr);
            anyAttr = true;
        }
        addRow(col, attrLabel(a.key), a.value);
    }

    col->addStretch(1);
    scroll->setWidget(body);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
    outer->addWidget(scroll, 1);
}

void ChartObjectInfoWindow::addRow(QVBoxLayout* col, const QString& field,
                                   const QString& value) {
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    auto* f = new QLabel(field);
    f->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted()));
    f->setMinimumWidth(120);
    auto* v = new QLabel(value);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(f, 0, Qt::AlignTop);
    row->addWidget(v, 1);
    col->addLayout(row);
}
