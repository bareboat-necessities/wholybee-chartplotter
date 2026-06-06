#include "simulator.hpp"
#include "nav_data_store.hpp"

#include <QTimer>
#include <QDateTime>
#include <QtMath>
#include <cmath>

namespace {
constexpr double kEarthRadiusM = 6378137.0;
constexpr double kDeg2Rad      = M_PI / 180.0;
constexpr double kRad2Deg      = 180.0 / M_PI;
constexpr double kKnotsToMps   = 1852.0 / 3600.0;
constexpr int    kTickMs       = 200;        // 5 Hz
} // namespace

Simulator::Simulator(INavDataPublisher* publisher, QObject* parent)
    : QObject(parent), publisher_(publisher) {
    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &Simulator::tick);
}

void Simulator::setRunning(bool on) {
    if (on == running_) return;
    running_ = on;
    if (running_) timer_->start();
    else          timer_->stop();
    emit runningChanged(running_);
}

void Simulator::setPosition(double latDeg, double lonDeg) {
    lat_ = latDeg;
    lon_ = lonDeg;
    emit positionChanged(lat_, lon_);
}

void Simulator::setHeading(double degTrue) { headingDeg_ = degTrue; }
void Simulator::setSpeed(double knots)     { sogKn_ = knots; }

void Simulator::tick() {
    // Move along a great-circle bearing on a sphere — good enough for an
    // illustrative scripted track and behaves correctly across the date line
    // and at high latitudes (no Mercator stretching).
    const double dt = kTickMs / 1000.0;
    const double dist = sogKn_ * kKnotsToMps * dt;          // metres this tick
    const double ang  = dist / kEarthRadiusM;               // angular distance

    const double lat1 = lat_ * kDeg2Rad;
    const double lon1 = lon_ * kDeg2Rad;
    const double brg  = headingDeg_ * kDeg2Rad;

    const double sinLat2 = std::sin(lat1) * std::cos(ang) +
                           std::cos(lat1) * std::sin(ang) * std::cos(brg);
    const double lat2 = std::asin(sinLat2);
    const double y = std::sin(brg) * std::sin(ang) * std::cos(lat1);
    const double x = std::cos(ang) - std::sin(lat1) * sinLat2;
    double lon2 = lon1 + std::atan2(y, x);

    lat_ = lat2 * kRad2Deg;
    lon_ = std::fmod(lon2 * kRad2Deg + 540.0, 360.0) - 180.0;   // wrap to (-180,180]
    emit positionChanged(lat_, lon_);

    if (publisher_) {
        NavValueMeta m;
        m.source = QStringLiteral("simulator");
        m.timestampUtc = QDateTime::currentDateTimeUtc();
        publisher_->publishOwnshipPosition(lat_, lon_, m);
        publisher_->publishCogSog(headingDeg_, sogKn_, m);
        publisher_->publishHeading(headingDeg_, std::nullopt, m);
    }
}
