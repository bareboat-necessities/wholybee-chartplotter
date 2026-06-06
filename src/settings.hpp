#pragma once
#include <QObject>
#include <QString>
#include <QVector>

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

public slots:
    void setChartDirectory(const QString& dir);
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);
    void setChartSets(const QVector<ChartSet>& sets);
    void setView(double lon, double lat, double scale);
    void setBasemapDirectory(const QString& dir);

signals:
    void chartDirectoryChanged(const QString& dir);
    void showSoundingsChanged(bool on);
    void showSymbolsChanged(bool on);
    void showDepthContoursChanged(bool on);
    void chartSetsChanged();
    void basemapDirectoryChanged(const QString& dir);

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
};
