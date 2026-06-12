#include "ais_overlay.hpp"
#include "ais_target_store.hpp"
#include "nav_data_store.hpp"
#include "vessel_symbol.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <cmath>

namespace {
constexpr double kMetresPerNm  = 1852.0;
constexpr double kSecondsPerMin = 60.0;

// Yellow disc with a grey rim, centred on a dangerous target and sized so the
// glyph sits inside it (diameter = 1.5x the glyph length). Drawn before the
// glyph so the symbol lands on top. Works in device pixels like drawSymbol.
void drawDangerHighlight(QPainter& p, const QPointF& pos, double scale) {
    p.save();
    p.resetTransform();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double r = 0.75 * vessel::kGlyphLengthPx * scale;   // 1.5x length / 2
    QPen rim(QColor(110, 110, 110));   // grey perimeter
    rim.setWidthF(1.5);
    p.setPen(rim);
    p.setBrush(QColor(255, 214, 0, 200));   // yellow, slightly translucent
    p.drawEllipse(pos, r, r);
    p.restore();
}

// Graphics for a dangerous encounter: a bold dashed red track from each
// vessel's bow to the point it will occupy at TCPA, a blue dot at each of
// those two points, and a red-edged yellow bar joining the dots — the CPA
// separation itself. Drawn in device pixels like drawSymbol, before the
// glyphs so the vessels land on top of their own track lines.
void drawCpaEncounter(QPainter& p,
                      const QPointF& tgtNow, const QPointF& tgtCpa,
                      const QPointF& ownNow, const QPointF& ownCpa,
                      double scale) {
    p.save();
    p.resetTransform();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Track legs. Start just ahead of each glyph (the bow tip sits ~14 px from
    // the centre at nominal scale) so the line reads as leaving the bow; skip a
    // leg entirely once the vessel is closer to its CPA point than that.
    const double bowPx = 14.0 * scale;
    QPen track(QColor(210, 25, 25));
    track.setWidthF(3.0);
    track.setStyle(Qt::DashLine);
    track.setCapStyle(Qt::FlatCap);
    p.setPen(track);
    auto trackLeg = [&p, bowPx](const QPointF& now, const QPointF& cpa) {
        const QPointF d = cpa - now;
        const double len = std::hypot(d.x(), d.y());
        if (len <= bowPx) return;
        p.drawLine(now + d * (bowPx / len), cpa);
    };
    trackLeg(tgtNow, tgtCpa);
    trackLeg(ownNow, ownCpa);

    // The CPA bar: solid yellow with a dashed red line centered on top,
    // both the same width as the track legs.
    QPen cpaYellow(QColor(255, 214, 0));
    cpaYellow.setWidthF(3.0);
    cpaYellow.setCapStyle(Qt::RoundCap);
    p.setPen(cpaYellow);
    p.drawLine(ownCpa, tgtCpa);
    QPen cpaDash(QColor(210, 25, 25));
    cpaDash.setWidthF(3.0);
    cpaDash.setStyle(Qt::DashLine);
    cpaDash.setCapStyle(Qt::FlatCap);
    p.setPen(cpaDash);
    p.drawLine(ownCpa, tgtCpa);

    // Blue dots marking the two CPA positions, capping the bar and the legs.
    const double r = 6.0 * scale;
    p.setPen(QPen(QColor(10, 25, 90), 1.5));
    p.setBrush(QColor(35, 70, 220));
    p.drawEllipse(ownCpa, r, r);
    p.drawEllipse(tgtCpa, r, r);

    p.restore();
}
} // namespace

void AisOverlay::paint(QPainter& p, const ChartViewport& vp) {
    if (!store_ || !visible_) return;

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
    // Dangerous targets: red fill, same shapes. A yellow highlight ring is added
    // behind them so they stand out regardless of the chart underneath.
    static const vessel::SymbolStyle kAisADanger{
        vessel::SymbolStyle::Shape::FilledTriangle,
        QColor(225, 35, 35),                  // current fill (red)
        QColor(205, 120, 120, 200),           // stale fill (dimmed red)
        QColor(70, 0, 0),                     // outline (current)
        QColor(70, 0, 0),                     // outline (stale)
        QColor(20, 20, 20, 220)               // predictor line
    };
    static const vessel::SymbolStyle kAisBDanger{
        vessel::SymbolStyle::Shape::Chevron,
        QColor(225, 35, 35),
        QColor(205, 120, 120, 200),
        QColor(70, 0, 0),
        QColor(70, 0, 0),
        QColor(20, 20, 20, 220)
    };

    for (const AisTarget& t : store_->targets()) {
        if (!t.hasPosition()) continue;
        const QPointF pos = vp.geoToScreen(*t.latitudeDeg, *t.longitudeDeg);

        // Heading: prefer reported heading, fall back to COG.
        std::optional<double> headingDeg;
        if (t.headingDegTrue) headingDeg = *t.headingDegTrue;
        else if (t.cogDegTrue) headingDeg = *t.cogDegTrue;

        const bool danger = isDangerous(t);
        if (danger) {
            drawDangerHighlight(p, pos, vesselScale_);
            // CPA encounter graphics need a future encounter (TCPA still
            // ahead), the projected endpoints, and a live ownship fix to
            // anchor the ownship leg.
            if (nav_ && t.tcpaSeconds && *t.tcpaSeconds >= 0.0
                && t.cpaOwnshipPos && t.cpaTargetPos) {
                const OwnshipState& os = nav_->ownship();
                if (os.latitudeDeg.valid() && os.longitudeDeg.valid()) {
                    const QPointF ownNow = vp.geoToScreen(os.latitudeDeg.value,
                                                          os.longitudeDeg.value);
                    const QPointF ownCpa = vp.geoToScreen(t.cpaOwnshipPos->latDeg,
                                                          t.cpaOwnshipPos->lonDeg);
                    const QPointF tgtCpa = vp.geoToScreen(t.cpaTargetPos->latDeg,
                                                          t.cpaTargetPos->lonDeg);
                    drawCpaEncounter(p, pos, tgtCpa, ownNow, ownCpa, vesselScale_);
                }
            }
        }

        const vessel::SymbolStyle& style = danger
            ? (t.cls == AisClass::B ? kAisBDanger : kAisADanger)
            : (t.cls == AisClass::B ? kAisB       : kAisA);
        vessel::drawSymbol(p, pos, headingDeg,
                           t.sogKnots.value_or(0.0),
                           predMinutes_, vp.pixelsPerMetre(),
                           t.freshness == AisFreshness::Stale, style,
                           vesselScale_);
    }
}

bool AisOverlay::isDangerous(const AisTarget& t) const {
    // CPA is the base trigger; with it off, nothing is flagged dangerous.
    if (!danger_.cpaEnabled || !t.cpaMeters) return false;

    // Far-away pre-filter: a target beyond the range limit is never dangerous,
    // however close its (geometric) CPA may be.
    if (danger_.ignoreFarEnabled && t.rangeMeters
        && *t.rangeMeters > danger_.ignoreFarNm * kMetresPerNm)
        return false;

    // CPA must be inside the limit.
    if (*t.cpaMeters >= danger_.cpaNm * kMetresPerNm) return false;

    // Optional TCPA gate: the closest approach must be ahead (TCPA >= 0; a
    // negative value means it has already passed and the target is opening) and
    // within the time window.
    if (danger_.tcpaEnabled) {
        if (!t.tcpaSeconds) return false;
        const double tcpa = *t.tcpaSeconds;
        if (tcpa < 0.0 || tcpa >= danger_.tcpaMin * kSecondsPerMin) return false;
    }
    return true;
}

bool AisOverlay::hitTest(const QPointF& screenPt) {
    if (!store_ || !visible_ || !haveViewport_ || !onClick_) return false;
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
