#include "instrument_tile.hpp"
#include "nav_data_store.hpp"

#include <QPainter>
#include <QPolygonF>
#include <QFont>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// Base tile size at scale 1.0; the bar scales every tile by the same factor.
constexpr double kBaseW = 128.0;
constexpr double kBaseH = 106.0;

// Gauge sweep: 270 degrees centred on straight-up, leaving a 90-degree gap at
// the bottom. Angles below are "clockwise from 12 o'clock", matching the CDI in
// the core nav display window.
constexpr double kSweepDeg = 270.0;
constexpr double kStartDeg = -135.0;   // gauge minimum (lower-left)
constexpr double kDeg2Rad  = 3.14159265358979323846 / 180.0;

QColor captionColor()      { return QColor(230, 233, 238, 165); }
QColor unitColor()         { return QColor(230, 233, 238, 150); }
QColor valueColor(int f)   { return f == 0 ? QColor(0xe6, 0xe9, 0xee)
                                           : QColor(230, 233, 238, 110); }  // dim when stale
QColor needleColor(int f)  { return f == 0 ? QColor(57, 211, 83)
                                           : QColor(57, 211, 83, 120); }
QColor trackColor()        { return QColor(255, 255, 255, 45); }
QColor tileBgColor()       { return QColor(255, 255, 255, 18); }

// A point on the gauge: `bearing` clockwise from the top, radius `r` from C.
QPointF gaugePt(const QPointF& c, double r, double bearing) {
    const double a = bearing * kDeg2Rad;
    return QPointF(c.x() + r * std::sin(a), c.y() - r * std::cos(a));
}

double normalize360(double d) {
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

QColor ringColor(int f)  { return f == 0 ? QColor(210, 214, 220) : QColor(150, 155, 163); }
QColor tickColor(int f)  { return QColor(150, 155, 163, f == 0 ? 255 : 160); }
QColor cardColor(int f)  { return QColor(235, 238, 244, f == 0 ? 255 : 150); }
QColor bowColor(int f)   { return f == 0 ? QColor(255, 210, 74) : QColor(255, 210, 74, 130); }
QColor windRedZone(int f)   { return f == 0 ? QColor(224, 72, 67) : QColor(224, 72, 67, 150); }
QColor windGreenZone(int f) { return f == 0 ? QColor(64, 196, 99) : QColor(64, 196, 99, 150); }

// Wind angles in the nav store are absolute (0..360, 0 = ahead). The wind gauge
// shows them relative to the bow: 0..180 is unchanged (starboard, positive);
// 180..360 has 360 subtracted (port, negative), giving a -180..+180 range.
double relativeWindAngle(double absDeg) {
    const double n = normalize360(absDeg);
    return (n <= 180.0) ? n : n - 360.0;
}
}  // namespace

InstrumentTile::InstrumentTile(const InstrumentDef& def, const NavDataStore* store,
                               QWidget* parent)
    : QFrame(parent), def_(def), store_(store) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);   // drags hit the bar
    applySize();
}

void InstrumentTile::setScale(double s) {
    scale_ = std::clamp(s, 0.4, 4.0);
    applySize();
    update();
}

void InstrumentTile::applySize() {
    setFixedSize(qRound(kBaseW * scale_), qRound(kBaseH * scale_));
}

void InstrumentTile::refresh() { update(); }

void InstrumentTile::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double pad = 3.0 * scale_;
    const QRectF body = QRectF(rect()).adjusted(pad, pad, -pad, -pad);

    // Tile card so each read-out reads as its own panel within the bar.
    p.setPen(Qt::NoPen);
    p.setBrush(tileBgColor());
    p.drawRoundedRect(body, 6.0 * scale_, 6.0 * scale_);

    // Resolve the live value + freshness.
    const NavValue* nv = store_ ? navValueForPath(store_->ownship(), def_.path) : nullptr;
    const bool haveValue = nv && nv->valid();
    const int  freshness = !haveValue ? 2 : (nv->stale() ? 1 : 0);
    const double display = nv ? nv->value * def_.factor : 0.0;
    const QString valueText = haveValue
        ? QString::number(display, 'f', std::max(0, def_.decimals))
        : QStringLiteral("—");   // em dash

    if (!def_.analog) {
        paintDigital(p, body, valueText, freshness);
        return;
    }

    switch (def_.gauge) {
    case GaugeStyle::Compass:
        paintCompass(p, body, haveValue, display, freshness);
        break;
    case GaugeStyle::Wind:
        paintWind(p, body, haveValue, display, freshness);
        break;
    case GaugeStyle::Arc:
    default: {
        double fraction = 0.0;
        if (haveValue && def_.max > def_.min)
            fraction = std::clamp((display - def_.min) / (def_.max - def_.min), 0.0, 1.0);
        paintArc(p, body, valueText, haveValue, fraction, freshness);
        break;
    }
    }
}

void InstrumentTile::paintDigital(QPainter& p, const QRectF& r,
                                  const QString& valueText, int freshness) {
    QFont f = p.font();

    // Caption (top).
    f.setBold(true);
    f.setPixelSize(std::max(9, qRound(13.0 * scale_)));
    p.setFont(f);
    p.setPen(captionColor());
    const QRectF capR(r.left(), r.top() + 4 * scale_, r.width(), 18 * scale_);
    p.drawText(capR, Qt::AlignHCenter | Qt::AlignTop, def_.caption);

    // Value (centre, large).
    f.setBold(true);
    f.setPixelSize(std::max(14, qRound(30.0 * scale_)));
    p.setFont(f);
    p.setPen(valueColor(freshness));
    const QRectF valR(r.left(), r.top(), r.width(), r.height() - 8 * scale_);
    p.drawText(valR, Qt::AlignCenter, valueText);

    // Unit (bottom).
    if (!def_.unit.isEmpty()) {
        f.setBold(false);
        f.setPixelSize(std::max(8, qRound(12.0 * scale_)));
        p.setFont(f);
        p.setPen(unitColor());
        const QRectF unitR(r.left(), r.bottom() - 18 * scale_, r.width(), 16 * scale_);
        p.drawText(unitR, Qt::AlignHCenter | Qt::AlignBottom, def_.unit);
    }
}

void InstrumentTile::paintArc(QPainter& p, const QRectF& r, const QString& valueText,
                              bool haveValue, double fraction, int freshness) {
    QFont f = p.font();

    // Caption (top).
    f.setBold(true);
    f.setPixelSize(std::max(9, qRound(12.0 * scale_)));
    p.setFont(f);
    p.setPen(captionColor());
    const QRectF capR(r.left(), r.top() + 3 * scale_, r.width(), 16 * scale_);
    p.drawText(capR, Qt::AlignHCenter | Qt::AlignTop, def_.caption);

    // Gauge geometry: a band below the caption, leaving room for the value text.
    const double topReserve = 18.0 * scale_;
    const double botReserve = 16.0 * scale_;
    const double avail = std::min(r.width(), r.height() - topReserve - botReserve);
    const double radius = std::max(8.0, avail / 2.0 - 2.0 * scale_);
    const QPointF c(r.center().x(), r.top() + topReserve + radius);

    // Track arc. QPainter angles are 1/16 degree, CCW from 3 o'clock; convert our
    // "clockwise-from-top" sweep: start maps to 90 - kStartDeg, span negates.
    const QRectF arcRect(c.x() - radius, c.y() - radius, 2 * radius, 2 * radius);
    const int startQt = qRound((90.0 - kStartDeg) * 16.0);
    const int spanQt  = qRound(-kSweepDeg * 16.0);
    QPen track(trackColor(), std::max(2.0, 3.0 * scale_), Qt::SolidLine, Qt::FlatCap);
    p.setBrush(Qt::NoBrush);
    p.setPen(track);
    p.drawArc(arcRect, startQt, spanQt);

    // Filled portion up to the current value, in the needle colour.
    if (haveValue && fraction > 0.0) {
        QPen fill(needleColor(freshness), std::max(2.0, 3.0 * scale_),
                  Qt::SolidLine, Qt::FlatCap);
        p.setPen(fill);
        p.drawArc(arcRect, startQt, qRound(-kSweepDeg * fraction * 16.0));
    }

    // Needle.
    if (haveValue) {
        const double bearing = kStartDeg + fraction * kSweepDeg;
        const QPointF tip = gaugePt(c, radius - 2.0 * scale_, bearing);
        QPen needle(needleColor(freshness), std::max(2.0, 2.5 * scale_),
                    Qt::SolidLine, Qt::RoundCap);
        p.setPen(needle);
        p.drawLine(c, tip);
        p.setPen(Qt::NoPen);
        p.setBrush(needleColor(freshness));
        p.drawEllipse(c, 3.0 * scale_, 3.0 * scale_);
    }

    // Numeric value + unit, centred just inside the gauge gap.
    f.setBold(true);
    f.setPixelSize(std::max(11, qRound(17.0 * scale_)));
    p.setFont(f);
    p.setPen(valueColor(freshness));
    const QString text = def_.unit.isEmpty() ? valueText
        : (haveValue ? valueText + QLatin1Char(' ') + def_.unit : valueText);
    const QRectF valR(r.left(), r.bottom() - 22 * scale_, r.width(), 20 * scale_);
    p.drawText(valR, Qt::AlignCenter, text);
}

// Compass ring + tick marks, shared by the compass-rose and wind gauges. Ticks
// every 30 degrees, emphasised at the cardinals (every 90).
void InstrumentTile::drawRoseRing(QPainter& p, const QPointF& c, double radius,
                                  int freshness) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ringColor(freshness), std::max(1.0, 1.5 * scale_)));
    p.drawEllipse(c, radius, radius);
    for (int b = 0; b < 360; b += 30) {
        const bool major = (b % 90 == 0);
        const double inset = (major ? 9.0 : 5.0) * scale_;
        p.setPen(QPen(major ? ringColor(freshness) : tickColor(freshness),
                      major ? std::max(1.5, 2.0 * scale_) : 1.0));
        p.drawLine(gaugePt(c, radius, b), gaugePt(c, radius - inset, b));
    }
}

// Geometry shared by both rose gauges: caption band on top, value band at the
// bottom, the largest ring that fits between. Fills `c` / `radius`.
namespace {
void roseGeometry(const QRectF& r, double scale, QPointF& c, double& radius) {
    const double topReserve = 16.0 * scale;
    const double botReserve = 16.0 * scale;
    const double avail = std::min(r.width(), r.height() - topReserve - botReserve);
    radius = std::max(8.0, avail / 2.0 - 2.0 * scale);
    c = QPointF(r.center().x(), r.top() + topReserve + radius);
}
}  // namespace

void InstrumentTile::paintCompass(QPainter& p, const QRectF& r, bool haveValue,
                                  double bearingDeg, int freshness) {
    QFont f = p.font();

    // Caption (top).
    f.setBold(true);
    f.setPixelSize(std::max(9, qRound(12.0 * scale_)));
    p.setFont(f);
    p.setPen(captionColor());
    p.drawText(QRectF(r.left(), r.top() + 3 * scale_, r.width(), 15 * scale_),
               Qt::AlignHCenter | Qt::AlignTop, def_.caption);

    QPointF c; double radius;
    roseGeometry(r, scale_, c, radius);
    drawRoseRing(p, c, radius, freshness);

    // Cardinal letters, north up.
    f.setBold(true);
    f.setPixelSize(std::max(8, qRound(10.0 * scale_)));
    p.setFont(f);
    p.setPen(cardColor(freshness));
    static const char* card[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; ++i) {
        const QPointF lp = gaugePt(c, radius - 11 * scale_, i * 90.0);
        p.drawText(QRectF(lp.x() - 8 * scale_, lp.y() - 8 * scale_, 16 * scale_, 16 * scale_),
                   Qt::AlignCenter, QString::fromLatin1(card[i]));
    }

    // Needle points to the bearing (north-up ring, so directly at the value).
    if (haveValue) {
        const double b = normalize360(bearingDeg);
        const QPointF tip  = gaugePt(c, radius - 3 * scale_, b);
        const QPointF tail = gaugePt(c, (radius - 3 * scale_) * 0.42, b + 180.0);
        p.setPen(QPen(needleColor(freshness), std::max(2.0, 2.5 * scale_),
                      Qt::SolidLine, Qt::RoundCap));
        p.drawLine(tail, tip);
        p.setPen(Qt::NoPen);
        p.setBrush(needleColor(freshness));
        p.drawEllipse(c, 3.0 * scale_, 3.0 * scale_);
    }

    // Numeric bearing + unit, just below the ring.
    f.setBold(true);
    f.setPixelSize(std::max(11, qRound(15.0 * scale_)));
    p.setFont(f);
    p.setPen(valueColor(freshness));
    QString text = QStringLiteral("—");
    if (haveValue) {
        text = QString::number(normalize360(bearingDeg), 'f', std::max(0, def_.decimals));
        if (!def_.unit.isEmpty()) text += QLatin1Char(' ') + def_.unit;
    }
    p.drawText(QRectF(r.left(), r.bottom() - 20 * scale_, r.width(), 18 * scale_),
               Qt::AlignCenter, text);
}

void InstrumentTile::paintWind(QPainter& p, const QRectF& r, bool haveValue,
                               double angleDeg, int freshness) {
    QFont f = p.font();

    // Caption (top).
    f.setBold(true);
    f.setPixelSize(std::max(9, qRound(12.0 * scale_)));
    p.setFont(f);
    p.setPen(captionColor());
    p.drawText(QRectF(r.left(), r.top() + 3 * scale_, r.width(), 15 * scale_),
               Qt::AlignHCenter | Qt::AlignTop, def_.caption);

    QPointF c; double radius;
    roseGeometry(r, scale_, c, radius);
    drawRoseRing(p, c, radius, freshness);

    // Colour the scale by angle off the bow: the ±0-30 band (across the bow) is
    // the red no-go zone; the ±30-60 bands either side are green. Drawn as arc
    // segments over the ring. Bearings are clockwise from the top; Qt arc angles
    // are 1/16 degree CCW from 3 o'clock, so bearing b -> (90 - b)*16.
    {
        const QRectF arcRect(c.x() - radius, c.y() - radius, 2 * radius, 2 * radius);
        const double w = std::max(3.0, 4.0 * scale_);
        const auto zone = [&](double b1, double b2, const QColor& col) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(col, w, Qt::SolidLine, Qt::FlatCap));
            p.drawArc(arcRect, qRound((90.0 - b1) * 16.0), qRound(-(b2 - b1) * 16.0));
        };
        zone(-30.0,  30.0, windRedZone(freshness));    // ±0-30: no-go (red)
        zone( 30.0,  60.0, windGreenZone(freshness));  // +30-60 starboard (green)
        zone(-60.0, -30.0, windGreenZone(freshness));  // -30-60 port (green)
    }

    // Bow-relative labels: 0 at the top (bow), +90 starboard (right), 180 astern,
    // -90 to port (left). The sign convention is the whole point of this gauge.
    f.setBold(false);
    f.setPixelSize(std::max(8, qRound(9.0 * scale_)));
    p.setFont(f);
    p.setPen(cardColor(freshness));
    struct Lbl { double bearing; const char* text; };
    static const Lbl labels[4] = {
        { 0.0, "0" }, { 90.0, "+90" }, { 180.0, "180" }, { 270.0, "-90" },
    };
    for (const Lbl& l : labels) {
        const QPointF lp = gaugePt(c, radius - 12 * scale_, l.bearing);
        p.drawText(QRectF(lp.x() - 12 * scale_, lp.y() - 8 * scale_, 24 * scale_, 16 * scale_),
                   Qt::AlignCenter, QString::fromLatin1(l.text));
    }

    // Bow marker: a small triangle at the top of the ring, pointing up.
    {
        const QPointF apex = gaugePt(c, radius, 0.0);
        QPolygonF bow;
        bow << apex
            << gaugePt(c, radius - 9 * scale_, 0.0) + QPointF(-5 * scale_, 0.0)
            << gaugePt(c, radius - 9 * scale_, 0.0) + QPointF( 5 * scale_, 0.0);
        p.setPen(Qt::NoPen);
        p.setBrush(bowColor(freshness));
        p.drawPolygon(bow);
    }

    // Needle points at the bow-relative angle, so a value to starboard (+)
    // swings right and a value to port (-) swings left.
    const double rel = relativeWindAngle(angleDeg);
    if (haveValue) {
        const QPointF tip  = gaugePt(c, radius - 3 * scale_, rel);
        const QPointF tail = gaugePt(c, (radius - 3 * scale_) * 0.42, rel + 180.0);
        p.setPen(QPen(needleColor(freshness), std::max(2.0, 2.5 * scale_),
                      Qt::SolidLine, Qt::RoundCap));
        p.drawLine(tail, tip);
        p.setPen(Qt::NoPen);
        p.setBrush(needleColor(freshness));
        p.drawEllipse(c, 3.0 * scale_, 3.0 * scale_);
    }

    // Signed numeric read-out: starboard positive, port negative.
    f.setBold(true);
    f.setPixelSize(std::max(11, qRound(15.0 * scale_)));
    p.setFont(f);
    p.setPen(valueColor(freshness));
    QString text = QStringLiteral("—");
    if (haveValue) {
        text = QString::number(rel, 'f', std::max(0, def_.decimals));
        if (!def_.unit.isEmpty()) text += QLatin1Char(' ') + def_.unit;
    }
    p.drawText(QRectF(r.left(), r.bottom() - 20 * scale_, r.width(), 18 * scale_),
               Qt::AlignCenter, text);
}
