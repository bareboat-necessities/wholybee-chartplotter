#pragma once
#include <QFrame>
#include "instrument_config.hpp"

class NavDataStore;

// A single instrument read-out: a digital value or an analogue arc gauge for one
// nav-data field. Custom-painted so digital and analogue tiles share one look and
// scale cleanly. The tile is transparent to mouse events so taps/drags fall
// through to the instrument bar that hosts it.
//
// Freshness follows the nav-store convention: a fresh value is bright, a stale
// value is dimmed, and an invalid/absent value reads as "—".
class InstrumentTile : public QFrame {
    Q_OBJECT
public:
    InstrumentTile(const InstrumentDef& def, const NavDataStore* store, QWidget* parent);

    void setScale(double s);          // 1.0 = base size; re-applies fixed size
    void refresh();                   // re-read the store and repaint

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void applySize();
    void paintDigital(QPainter& p, const QRectF& r, const QString& valueText,
                      int freshness);
    void paintArc(QPainter& p, const QRectF& r, const QString& valueText,
                  bool haveValue, double fraction, int freshness);
    // Rose gauges: a full ring with the bearing/angle as a rotating needle.
    void paintCompass(QPainter& p, const QRectF& r, bool haveValue,
                      double bearingDeg, int freshness);
    void paintWind(QPainter& p, const QRectF& r, bool haveValue,
                   double angleDeg, int freshness);
    // Shared ring + tick marks for the rose gauges. Returns nothing; the caller
    // owns the centre / radius it computed.
    void drawRoseRing(QPainter& p, const QPointF& c, double radius, int freshness);

    InstrumentDef       def_;
    const NavDataStore* store_ = nullptr;
    double              scale_ = 1.0;
};
