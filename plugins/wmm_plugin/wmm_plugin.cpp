#include "wmm_plugin.hpp"
#include "nav_data_store.hpp"

#include <QTimer>
#include <QDate>
#include <QDateTime>
#include <QtGlobal>

namespace {
constexpr int kIntervalMs = 4000;   // recompute every 4 seconds

// Fractional ("decimal") year for the WMM time argument, e.g. 2026.45.
double decimalYear(const QDate& d) {
    const int year = d.year();
    const int days = QDate(year, 12, 31).dayOfYear();   // 365 or 366
    return year + double(d.dayOfYear() - 1) / double(days);
}
}  // namespace

WmmPlugin::WmmPlugin() = default;
WmmPlugin::~WmmPlugin() = default;

void WmmPlugin::initialize(ICoreApi* core) {
    core_ = core;
    timer_ = std::make_unique<QTimer>();
    timer_->setInterval(kIntervalMs);
    QObject::connect(timer_.get(), &QTimer::timeout, timer_.get(),
                     [this] { computeAndPublish(); });
    timer_->start();
    computeAndPublish();   // also compute promptly at startup
}

void WmmPlugin::shutdown() {
    timer_.reset();        // stops the timer
    core_ = nullptr;
}

void WmmPlugin::computeAndPublish() {
    if (!core_) return;
    const NavDataStore*  nav = core_->navData();
    INavDataPublisher*   pub = core_->navPublisher();
    if (!nav || !pub) return;

    const OwnshipState& os = nav->ownship();
    if (!os.latitudeDeg.valid() || !os.longitudeDeg.valid())
        return;   // no position fix -> nothing to base the model on

    const double lat = os.latitudeDeg.value;
    const double lon = os.longitudeDeg.value;
    const double variation = model_.declination(lat, lon, 0.0,
                                                 decimalYear(QDate::currentDate()));

    NavValueMeta m;
    m.source = QStringLiteral("WMMplugin");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    pub->publishVariation(variation, m);

    if (!logged_) {
        logged_ = true;
        qInfo("WMM plugin: variation at %.4f, %.4f = %.2f deg (%s)",
              lat, lon, variation, variation >= 0.0 ? "E" : "W");
    }
}
