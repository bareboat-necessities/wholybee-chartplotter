#pragma once
#include <QColor>
#include <QPointF>
#include <optional>

class QPainter;

// Shared vessel glyph renderer. Drawn in device pixels at constant on-screen
// size: a shape pointing along heading (a circle when heading is unknown),
// an optional course-prediction line ahead, dimmed with a cancellation slash
// when stale.
namespace vessel {

struct SymbolStyle {
    // FilledTriangle — Class A / ownship: solid filled triangle.
    // Chevron        — Class B: open arrowhead (two forward sides only, no
    //                  base, no fill). Matches the IALA/IHO Class B convention.
    enum class Shape { FilledTriangle, Chevron };

    Shape  shape    = Shape::FilledTriangle;
    QColor fill;        // bright fill (FilledTriangle only)
    QColor staleFill;   // dimmed fill (FilledTriangle only)
    QColor edge;        // outline / chevron stroke colour (current)
    QColor staleEdge;   // outline / chevron stroke colour (stale)
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
