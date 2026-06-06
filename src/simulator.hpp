#pragma once
#include <QObject>

class INavDataPublisher;
class QTimer;

// Scripted ownship: a constant-heading, constant-speed straight track,
// publishing position / COG-SOG / heading to the nav store at ~5 Hz.
//
// First built-in nav source per ProjectSpec.md's "simulator plugin early"
// recommendation. Lets us build ownship rendering, stale-data handling, and
// later instruments/AIS/routes without live marine electronics. Uses the same
// INavDataPublisher API that a dynamic plugin would target later.
class Simulator : public QObject {
    Q_OBJECT
public:
    Simulator(INavDataPublisher* publisher, QObject* parent = nullptr);

    bool   isRunning() const { return running_; }
    double latitudeDeg()  const { return lat_; }
    double longitudeDeg() const { return lon_; }
    double headingDeg()   const { return headingDeg_; }
    double sogKnots()     const { return sogKn_; }

public slots:
    void setRunning(bool on);
    // Place the boat. Used both to set the persisted start point and to reset.
    void setPosition(double latDeg, double lonDeg);
    void setHeading(double degTrue);
    void setSpeed(double knots);

signals:
    void runningChanged(bool on);
    void positionChanged(double latDeg, double lonDeg);

private slots:
    void tick();

private:
    INavDataPublisher* publisher_ = nullptr;
    QTimer* timer_ = nullptr;

    bool   running_ = false;
    double lat_ = 38.0;             // San Francisco area default
    double lon_ = -123.0;
    double headingDeg_ = 270.0;     // due west, into the Pacific
    double sogKn_ = 8.0;            // ~typical cruising speed
};
