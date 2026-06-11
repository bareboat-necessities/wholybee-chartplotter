#pragma once
#include <QColor>
#include <QPointF>
#include <optional>

class QPainter;

// Shared vessel glyph, so ownship and AIS targets render identically (only the
// colours differ). Drawn in device pixels at constant on-screen size: a triangle
// pointing along heading (a circle when heading is unknown), an optional
// course-prediction line ahead, dimmed with a cancellation slash when stale.
namespace vessel {

struct SymbolStyle {
    QColor fill;        // bright fill (current data)
    QColor staleFill;   // dimmed fill (stale data)
    QColor edge;        // triangle/circle outline
    QColor predLine;    // course-prediction line
};

// `pos` is the device-pixel position. `headingDeg` absent => draw a circle and
// no rotation. The prediction line reaches where the vessel will be in
// `predMinutes` at `sogKnots`, using `pixelsPerMetre` to scale. `scale`
// multiplies all on-screen pixel sizes uniformly (1.0 = nominal).
void drawSymbol(QPainter& p, const QPointF& pos,
                std::optional<double> headingDeg,
                double sogKnots, double predMinutes, double pixelsPerMetre,
                bool stale, const SymbolStyle& style, double scale = 1.0);

} // namespace vessel
