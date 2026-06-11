#include "ais_overlay.hpp"
#include "ais_target_store.hpp"
#include "vessel_symbol.hpp"

#include <QColor>

void AisOverlay::paint(QPainter& p, const ChartViewport& vp) {
    if (!store_) return;

    // Cache the camera snapshot so hitTest can project target positions without
    // re-deriving the view geometry.
    lastViewport_ = vp;
    haveViewport_ = true;

    // Class A: green filled triangle (same shape as ownship, different colour).
    static const vessel::SymbolStyle kAisA{
        vessel::SymbolStyle::Shape::FilledTriangle,
        QColor(30, 170, 60),                  // current fill
        QColor(120, 170, 130, 200),           // stale fill (dimmed)
        QColor(0, 60, 10),                    // outline (current)
        QColor(0, 60, 10),                    // outline (stale)
        QColor(20, 20, 20, 220)               // predictor line
    };
    // Class B: green filled arrowhead — IALA/IHO Class B convention.
    static const vessel::SymbolStyle kAisB{
        vessel::SymbolStyle::Shape::Chevron,
        QColor(30, 170, 60),                  // current fill
        QColor(120, 170, 130, 200),           // stale fill (dimmed)
        QColor(0, 60, 10),                    // outline (current)
        QColor(0, 60, 10),                    // outline (stale)
        QColor(20, 20, 20, 220)               // predictor line
    };

    for (const AisTarget& t : store_->targets()) {
        if (!t.hasPosition()) continue;
        const QPointF pos = vp.geoToScreen(*t.latitudeDeg, *t.longitudeDeg);

        // Heading: prefer reported heading, fall back to COG.
        std::optional<double> headingDeg;
        if (t.headingDegTrue) headingDeg = *t.headingDegTrue;
        else if (t.cogDegTrue) headingDeg = *t.cogDegTrue;

        const vessel::SymbolStyle& style = (t.cls == AisClass::B) ? kAisB : kAisA;
        vessel::drawSymbol(p, pos, headingDeg,
                           t.sogKnots.value_or(0.0),
                           predMinutes_, vp.pixelsPerMetre(),
                           t.freshness == AisFreshness::Stale, style,
                           vesselScale_);
    }
}

bool AisOverlay::hitTest(const QPointF& screenPt) {
    if (!store_ || !haveViewport_ || !onClick_) return false;
    // Pick the target whose glyph centre is closest to the click, within the
    // glyph's roughly 14 px radius (scales with vessel size).
    const double kPickRadiusPx = 14.0 * vesselScale_;
    double bestSq = kPickRadiusPx * kPickRadiusPx;
    quint32 bestMmsi = 0;
    for (const AisTarget& t : store_->targets()) {
        if (!t.hasPosition()) continue;
        const QPointF q = lastViewport_.geoToScreen(*t.latitudeDeg, *t.longitudeDeg);
        const double dx = q.x() - screenPt.x(), dy = q.y() - screenPt.y();
        const double dSq = dx * dx + dy * dy;
        if (dSq < bestSq) { bestSq = dSq; bestMmsi = t.mmsi; }
    }
    if (bestMmsi == 0) return false;
    onClick_(bestMmsi);
    return true;
}
