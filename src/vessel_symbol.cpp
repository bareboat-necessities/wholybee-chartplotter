#include "vessel_symbol.hpp"
#include <QPainter>
#include <QPolygonF>
#include <QPen>

namespace vessel {

void drawSymbol(QPainter& p, const QPointF& pos,
                std::optional<double> headingDeg,
                double sogKnots, double predMinutes, double pixelsPerMetre,
                bool stale, const SymbolStyle& s, double scale) {
    p.save();
    p.resetTransform();                   // device pixels, constant size
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(pos);
    if (headingDeg) p.rotate(*headingDeg);
    if (scale != 1.0) p.scale(scale, scale);

    // Course-prediction line ahead (drawn first so the symbol sits on top): where
    // the vessel will be in predMinutes at the current SOG.
    if (sogKnots > 0.1 && pixelsPerMetre > 0.0 && predMinutes > 0.0) {
        const double dist_m = sogKnots * (1852.0 / 60.0) * predMinutes;
        // Prediction line length is a geographic distance — divide by scale so it
        // stays the same on-screen length regardless of the symbol size.
        const double len = (dist_m * pixelsPerMetre) / scale;
        if (len >= 1.0) {
            QPen line(s.predLine); line.setWidthF(1.5);
            p.setPen(line);
            p.drawLine(QPointF(0, 0), QPointF(0, -len));
        }
    }

    // Triangle pointing along heading (or a circle when heading is unknown).
    QPolygonF tri;
    tri << QPointF(0, -14) << QPointF(8, 8) << QPointF(-8, 8);
    QPen edge(s.edge); edge.setWidthF(1.2);
    p.setBrush(stale ? s.staleFill : s.fill);
    p.setPen(edge);
    if (headingDeg) p.drawPolygon(tri);
    else            p.drawEllipse(QPointF(0, 0), 7.0, 7.0);

    if (stale) {
        // Cancellation slash at right angles to the centerline (marine
        // convention for an unreliable / DR fix).
        QPen slash(QColor(0, 0, 0)); slash.setWidthF(1.6);
        p.setPen(slash);
        p.drawLine(QPointF(-8.0, 0.0), QPointF(8.0, 0.0));
    }
    p.restore();
}

} // namespace vessel
