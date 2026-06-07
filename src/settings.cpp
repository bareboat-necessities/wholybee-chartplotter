#include "settings.hpp"
#include <QSettings>
#include <QDir>
#include <cmath>

namespace {
constexpr auto kChartDir  = "charts/directory";
constexpr auto kChartSets = "charts/sets";
constexpr auto kSoundings = "display/showSoundings";
constexpr auto kSymbols   = "display/showSymbols";
constexpr auto kContours  = "display/showDepthContours";
constexpr auto kViewLon   = "view/centerLon";
constexpr auto kViewLat   = "view/centerLat";
constexpr auto kViewScale = "view/scale";
constexpr auto kBasemap   = "basemap/directory";
constexpr auto kSimOn     = "sim/enabled";
constexpr auto kSimLat    = "sim/lat";
constexpr auto kSimLon    = "sim/lon";
constexpr auto kStaleS    = "nav/staleSeconds";
constexpr auto kInvalidS  = "nav/invalidSeconds";
constexpr auto kPredMin   = "ships/ownshipPredictionMinutes";
constexpr auto kDepthUnit = "units/depth";
constexpr auto kDistUnit  = "units/distance";
} // namespace

Settings::Settings(QObject* parent) : QObject(parent) {
    QSettings s;
    chartDir_          = s.value(QLatin1String(kChartDir)).toString();
    showSoundings_     = s.value(QLatin1String(kSoundings), true).toBool();
    showSymbols_       = s.value(QLatin1String(kSymbols),   true).toBool();
    showDepthContours_ = s.value(QLatin1String(kContours),  true).toBool();
    viewLon_   = s.value(QLatin1String(kViewLon),   0.0).toDouble();
    viewLat_   = s.value(QLatin1String(kViewLat),   0.0).toDouble();
    viewScale_ = s.value(QLatin1String(kViewScale), 0.0).toDouble();
    basemapDir_ = s.value(QLatin1String(kBasemap)).toString();
    simEnabled_ = s.value(QLatin1String(kSimOn), false).toBool();
    simLat_     = s.value(QLatin1String(kSimLat), 37.9).toDouble();
    simLon_     = s.value(QLatin1String(kSimLon), -123.0).toDouble();
    // Migrate users who briefly tested the simulator when the default still put
    // the boat on land (38.0N, 123.0W). Latitude stays put under the due-west
    // heading, so a tight latitude match plus a westward lon up to ~120 nm
    // means "tested briefly" — reset; a real cruise will have left this window.
    if (std::abs(simLat_ - 38.0) < 0.05 && simLon_ <= -123.0 && simLon_ >= -125.0) {
        simLat_ = 37.9;
        simLon_ = -123.0;
        s.setValue(QLatin1String(kSimLat), simLat_);
        s.setValue(QLatin1String(kSimLon), simLon_);
    }
    staleSeconds_   = s.value(QLatin1String(kStaleS),   5.0).toDouble();
    invalidSeconds_ = s.value(QLatin1String(kInvalidS), 30.0).toDouble();
    ownshipPredMin_ = s.value(QLatin1String(kPredMin),  6.0).toDouble();
    depthUnit_    = units::depthUnitFromKey(s.value(QLatin1String(kDepthUnit)).toString(),
                                            DepthUnit::Feet);
    distanceUnit_ = units::distanceUnitFromKey(s.value(QLatin1String(kDistUnit)).toString(),
                                               DistanceUnit::NauticalMiles);
    loadChartSets();

    // Migrate a pre-chart-sets install: if no sets are defined yet but a chart
    // directory was remembered, seed one set from it so the existing setup keeps
    // working and appears in the menu.
    if (chartSets_.isEmpty() && !chartDir_.isEmpty()) {
        ChartSet cs;
        cs.directory = chartDir_;
        cs.name = QDir(chartDir_).dirName();
        if (cs.name.isEmpty()) cs.name = chartDir_;
        chartSets_.push_back(cs);
        saveChartSets();
    }
}

void Settings::loadChartSets() {
    chartSets_.clear();
    QSettings s;
    const int n = s.beginReadArray(QLatin1String(kChartSets));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        ChartSet cs;
        cs.name      = s.value(QLatin1String("name")).toString();
        cs.directory = s.value(QLatin1String("directory")).toString();
        if (!cs.directory.isEmpty()) chartSets_.push_back(cs);
    }
    s.endArray();
}

void Settings::saveChartSets() {
    QSettings s;
    // Clear first so a shorter list doesn't leave stale trailing entries behind.
    s.remove(QLatin1String(kChartSets));
    s.beginWriteArray(QLatin1String(kChartSets));
    for (int i = 0; i < chartSets_.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QLatin1String("name"),      chartSets_[i].name);
        s.setValue(QLatin1String("directory"), chartSets_[i].directory);
    }
    s.endArray();
}

void Settings::setChartSets(const QVector<ChartSet>& sets) {
    chartSets_ = sets;
    saveChartSets();
    emit chartSetsChanged();
}

void Settings::setView(double lon, double lat, double scale) {
    viewLon_ = lon; viewLat_ = lat; viewScale_ = scale;
    QSettings s;
    s.setValue(QLatin1String(kViewLon), lon);
    s.setValue(QLatin1String(kViewLat), lat);
    s.setValue(QLatin1String(kViewScale), scale);
}

void Settings::setSimulatorEnabled(bool on) {
    if (on == simEnabled_) return;
    simEnabled_ = on;
    QSettings().setValue(QLatin1String(kSimOn), on);
    emit simulatorEnabledChanged(on);
}

void Settings::setSimulatorPosition(double lat, double lon) {
    if (lat == simLat_ && lon == simLon_) return;
    simLat_ = lat; simLon_ = lon;
    QSettings s;
    s.setValue(QLatin1String(kSimLat), lat);
    s.setValue(QLatin1String(kSimLon), lon);
}

void Settings::setOwnshipPredictionMinutes(double minutes) {
    if (minutes == ownshipPredMin_) return;
    ownshipPredMin_ = minutes;
    QSettings().setValue(QLatin1String(kPredMin), minutes);
    emit ownshipPredictionMinutesChanged(minutes);
}

void Settings::setDepthUnit(DepthUnit u) {
    if (u == depthUnit_) return;
    depthUnit_ = u;
    QSettings().setValue(QLatin1String(kDepthUnit), units::depthUnitKey(u));
    emit depthUnitChanged(u);
}

void Settings::setDistanceUnit(DistanceUnit u) {
    if (u == distanceUnit_) return;
    distanceUnit_ = u;
    QSettings().setValue(QLatin1String(kDistUnit), units::distanceUnitKey(u));
    emit distanceUnitChanged(u);
}

void Settings::setStaleThresholds(double staleS, double invalidS) {
    if (staleS == staleSeconds_ && invalidS == invalidSeconds_) return;
    staleSeconds_   = staleS;
    invalidSeconds_ = invalidS;
    QSettings s;
    s.setValue(QLatin1String(kStaleS),   staleS);
    s.setValue(QLatin1String(kInvalidS), invalidS);
    emit staleThresholdsChanged(staleS, invalidS);
}

void Settings::setBasemapDirectory(const QString& dir) {
    if (dir == basemapDir_) return;
    basemapDir_ = dir;
    QSettings().setValue(QLatin1String(kBasemap), dir);
    emit basemapDirectoryChanged(dir);
}

void Settings::setChartDirectory(const QString& dir) {
    if (dir == chartDir_) return;
    chartDir_ = dir;
    QSettings().setValue(QLatin1String(kChartDir), dir);
    emit chartDirectoryChanged(dir);
}

void Settings::setShowSoundings(bool on) {
    if (on == showSoundings_) return;
    showSoundings_ = on;
    QSettings().setValue(QLatin1String(kSoundings), on);
    emit showSoundingsChanged(on);
}

void Settings::setShowSymbols(bool on) {
    if (on == showSymbols_) return;
    showSymbols_ = on;
    QSettings().setValue(QLatin1String(kSymbols), on);
    emit showSymbolsChanged(on);
}

void Settings::setShowDepthContours(bool on) {
    if (on == showDepthContours_) return;
    showDepthContours_ = on;
    QSettings().setValue(QLatin1String(kContours), on);
    emit showDepthContoursChanged(on);
}
