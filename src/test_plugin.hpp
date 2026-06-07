#pragma once
#include <QDialog>
#include <functional>
#include <memory>
#include "plugin_api.hpp"

class NavDataStore;
class INavDataPublisher;
class QLabel;
class QTimer;
class TouchSpinBox;

// Draws "Hello World" on the chart when enabled. Anchored to the ownship
// position via the viewport's geoToScreen when a fix exists (exercising the
// coordinate API), else centred in the viewport so it is always visible.
class HelloWorldOverlay : public IChartOverlay {
public:
    explicit HelloWorldOverlay(const NavDataStore* nav) : nav_(nav) {}
    void setEnabled(bool on) { enabled_ = on; }
    void paint(QPainter& painter, const ChartViewport& viewport) override;

private:
    const NavDataStore* nav_ = nullptr;
    bool enabled_ = false;
};

// Reads the current depth from the nav store (value, source, age — live) and
// lets the user publish a new depth value back into the store. Demonstrates both
// directions of the nav data API from a plugin.
class DepthEntryDialog : public QDialog {
    Q_OBJECT
public:
    DepthEntryDialog(const NavDataStore* store, INavDataPublisher* publisher,
                     QWidget* parent = nullptr);

private slots:
    void refresh();
    void publish();

private:
    const NavDataStore* store_ = nullptr;
    INavDataPublisher*  publisher_ = nullptr;
    QLabel*       current_ = nullptr;
    TouchSpinBox* input_ = nullptr;
    QTimer*       timer_ = nullptr;
};

// Built-in plugin exercising the API: a checkable "Hello World" overlay item, a
// "Publish Depth…" item that opens DepthEntryDialog, registration as a data
// source (auto Data Connections item + status dot), and a settings page (hosted
// by the core) with a single enable checkbox. While enabled as a source it
// publishes a varying depth at 1 Hz.
class TestPlugin : public IPlugin, public ISettingsPageProvider {
public:
    TestPlugin();             // out-of-line: unique_ptr<QTimer> (incomplete here)
    ~TestPlugin() override;

    QString name() const override { return QStringLiteral("Test Plugin"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

    // ISettingsPageProvider
    QString  settingsPageTitle() const override { return QStringLiteral("Test Plugin"); }
    QWidget* createSettingsPage(QWidget* parent) override;

private:
    void setSourceEnabled(bool on);
    void publishDepthTick();

    ICoreApi* core_ = nullptr;
    std::unique_ptr<HelloWorldOverlay> overlay_;
    IDataSource*     dataSource_ = nullptr;    // owned by the core
    IPluginSettings* settings_ = nullptr;      // owned by the core (persisted)
    std::unique_ptr<QTimer> publishTimer_;
    bool   sourceEnabled_ = false;
    double depthPhase_ = 0.0;
};
