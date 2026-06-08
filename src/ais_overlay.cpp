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

    // Green AIS glyph — same shape/predictor/slash as ownship, just a different
    // colour so the user can tell ownship from AIS at a glance.
    static const vessel::SymbolStyle kAis{
        QColor(30, 170, 60),                  // current fill
        QColor(120, 170, 130, 200),           // stale fill (dimmed)
        QColor(0, 60, 10),                    // outline
        QColor(20, 20, 20, 220)               // predictor line (same as ownship)
    };

    for (const AisTarget& t : store_->targets()) {
        if (!t.hasPosition()) continue;
        const QPointF pos = vp.geoToScreen(*t.latitudeDeg, *t.longitudeDeg);

        // Heading for the triangle: prefer reported heading, fall back to COG.
        std::optional<double> headingDeg;
        if (t.headingDegTrue) headingDeg = *t.headingDegTrue;
        else if (t.cogDegTrue) headingDeg = *t.cogDegTrue;

        vessel::drawSymbol(p, pos, headingDeg,
                           t.sogKnots.value_or(0.0),
                           predMinutes_, vp.pixelsPerMetre(),
                           t.freshness == AisFreshness::Stale, kAis);
    }
}

bool AisOverlay::hitTest(const QPointF& screenPt) {
    if (!store_ || !haveViewport_ || !onClick_) return false;
    // Pick the target whose glyph centre is closest to the click, within the
    // glyph's roughly 14 px radius (matches the triangle's extent).
    constexpr double kPickRadiusPx = 14.0;
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
