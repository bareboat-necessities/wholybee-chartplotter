#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include "units.hpp"
#include "nmea0183_client.hpp"   // NmeaTransport

// A named chart directory the user can switch between from the menu.
struct ChartSet {
    QString name;
    QString directory;
};

// Central, persistent application settings: the single source of truth for
// user-facing preferences, backed by QSettings.
//
// Components read current values and subscribe to the change signals rather
// than touching QSettings directly. This keeps settings consistent and
// observable, and matches the core/plugin model in ProjectSpec.md — the core
// owns shared state, everything else publishes and subscribes.
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    // The directory of the chart set currently loaded ("active").
    QString chartDirectory() const { return chartDir_; }
    bool showSoundings() const { return showSoundings_; }
    bool showSymbols() const { return showSymbols_; }
    bool showDepthContours() const { return showDepthContours_; }

    // The user's defined chart sets, in menu order.
    const QVector<ChartSet>& chartSets() const { return chartSets_; }

    // Folder holding the GSHHG basemap data (contains GSHHS_shp/). Empty means
    // "search the standard locations"; an explicit path also lets the user point
    // at a higher-resolution tier they dropped in.
    QString basemapDirectory() const { return basemapDir_; }

    // Last view location, so the app reopens where the user left off. The view
    // is the center in geographic degrees plus the zoom (scene pixels per metre).
    bool hasSavedView() const { return viewScale_ > 0.0; }
    double viewLon() const { return viewLon_; }
    double viewLat() const { return viewLat_; }
    double viewScale() const { return viewScale_; }

    // Simulator (the scripted built-in nav source).
    bool   simulatorEnabled() const { return simEnabled_; }
    double simulatorLat()     const { return simLat_; }
    double simulatorLon()     const { return simLon_; }

    // Ownship stale-data thresholds (seconds). The fix is considered Stale at
    // staleSeconds and Invalid (hidden) at invalidSeconds.
    double staleSeconds()   const { return staleSeconds_; }
    double invalidSeconds() const { return invalidSeconds_; }

    // Length of the ownship course-prediction line, in minutes of run-time at
    // the current SOG. Drawn from the bow along the boat's heading.
    double ownshipPredictionMinutes() const { return ownshipPredMin_; }

    // Display units. Depth drives how chart soundings are labelled; distance is
    // stored for upcoming range/route features and not consumed yet.
    DepthUnit    depthUnit()    const { return depthUnit_; }
    DistanceUnit distanceUnit() const { return distanceUnit_; }

    // NMEA 0183 network gateway connection.
    NmeaTransport nmeaTransport() const { return nmeaTransport_; }
    QString       nmeaHost()      const { return nmeaHost_; }
    quint16       nmeaPort()      const { return nmeaPort_; }
    bool          nmeaEnabled()   const { return nmeaEnabled_; }

public slots:
    void setChartDirectory(const QString& dir);
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);
    void setChartSets(const QVector<ChartSet>& sets);
    void setView(double lon, double lat, double scale);
    void setBasemapDirectory(const QString& dir);
    void setSimulatorEnabled(bool on);
    void setSimulatorPosition(double lat, double lon);
    void setStaleThresholds(double staleS, double invalidS);
    void setOwnshipPredictionMinutes(double minutes);
    void setDepthUnit(DepthUnit u);
    void setDistanceUnit(DistanceUnit u);
    void setNmeaConfig(NmeaTransport transport, const QString& host,
                       quint16 port, bool enabled);

signals:
    void chartDirectoryChanged(const QString& dir);
    void showSoundingsChanged(bool on);
    void showSymbolsChanged(bool on);
    void showDepthContoursChanged(bool on);
    void chartSetsChanged();
    void basemapDirectoryChanged(const QString& dir);
    void simulatorEnabledChanged(bool on);
    void staleThresholdsChanged(double staleS, double invalidS);
    void ownshipPredictionMinutesChanged(double minutes);
    void depthUnitChanged(DepthUnit u);
    void distanceUnitChanged(DistanceUnit u);
    void nmeaConfigChanged(NmeaTransport transport, const QString& host,
                           quint16 port, bool enabled);

private:
    void loadChartSets();
    void saveChartSets();

    QString chartDir_;
    bool showSoundings_ = true;
    bool showSymbols_ = true;
    bool showDepthContours_ = true;
    QVector<ChartSet> chartSets_;
    QString basemapDir_;
    double viewLon_ = 0.0;
    double viewLat_ = 0.0;
    double viewScale_ = 0.0;   // 0 => no saved view
    bool   simEnabled_ = false;
    double simLat_ = 37.9;     // just SW of Point Reyes, in open water
    double simLon_ = -123.0;
    double staleSeconds_   = 5.0;
    double invalidSeconds_ = 30.0;
    double ownshipPredMin_ = 6.0;   // minutes of run-time ahead
    DepthUnit    depthUnit_    = DepthUnit::Feet;
    DistanceUnit distanceUnit_ = DistanceUnit::NauticalMiles;
    NmeaTransport nmeaTransport_ = NmeaTransport::Tcp;
    QString       nmeaHost_;
    quint16       nmeaPort_ = 10110;   // IANA-registered NMEA-0183 port
    bool          nmeaEnabled_ = false;
};
