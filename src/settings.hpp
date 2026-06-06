#pragma once
#include <QObject>
#include <QString>

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

    QString chartDirectory() const { return chartDir_; }
    bool showSoundings() const { return showSoundings_; }
    bool showSymbols() const { return showSymbols_; }
    bool showDepthContours() const { return showDepthContours_; }

public slots:
    void setChartDirectory(const QString& dir);
    void setShowSoundings(bool on);
    void setShowSymbols(bool on);
    void setShowDepthContours(bool on);

signals:
    void chartDirectoryChanged(const QString& dir);
    void showSoundingsChanged(bool on);
    void showSymbolsChanged(bool on);
    void showDepthContoursChanged(bool on);

private:
    QString chartDir_;
    bool showSoundings_ = true;
    bool showSymbols_ = true;
    bool showDepthContours_ = true;
};
