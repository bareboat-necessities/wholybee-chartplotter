#include "settings.hpp"
#include <QSettings>

namespace {
constexpr auto kChartDir  = "charts/directory";
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
