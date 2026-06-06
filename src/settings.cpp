#include "settings.hpp"
#include <QSettings>
#include <QDir>

namespace {
constexpr auto kChartDir  = "charts/directory";
constexpr auto kChartSets = "charts/sets";
constexpr auto kSoundings = "display/showSoundings";
constexpr auto kSymbols   = "display/showSymbols";
constexpr auto kContours  = "display/showDepthContours";
} // namespace

Settings::Settings(QObject* parent) : QObject(parent) {
    QSettings s;
    chartDir_          = s.value(QLatin1String(kChartDir)).toString();
    showSoundings_     = s.value(QLatin1String(kSoundings), true).toBool();
    showSymbols_       = s.value(QLatin1String(kSymbols),   true).toBool();
    showDepthContours_ = s.value(QLatin1String(kContours),  true).toBool();
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
