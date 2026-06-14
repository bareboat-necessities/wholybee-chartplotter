#pragma once
#include <QString>
#include <memory>
#include "plugin_api.hpp"

class SignalKClient;
class SignalKDecoder;

// Signal K data source plugin. Connects to a Signal K server over a WebSocket
// and decodes the standard navigation values and AIS targets into the same
// publisher APIs the NMEA plugins use. Built as a separate DLL, loaded by the
// host at runtime via QPluginLoader (same path as plugins/test_plugin).
//
// Mirrors Nmea0183Plugin's shape: owns the client + decoder, registers as a
// data source (Data Connections item + green dot + Data Priority entry), and
// hosts a settings page for the connection config (host/port/enable).
class SignalKPlugin : public IPlugin, public ISettingsPageProvider {
public:
    SignalKPlugin();
    ~SignalKPlugin() override;

    QString name() const override { return QStringLiteral("Signal K"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

    QString  settingsPageTitle() const override { return QStringLiteral("Signal K"); }
    QWidget* createSettingsPage(QWidget* parent) override;

private:
    void loadConfig();
    void applyConfig(const QString& host, quint16 port, bool enabled);

    ICoreApi*        core_       = nullptr;
    IDataSource*     dataSource_ = nullptr;   // core-owned
    IPluginSettings* settings_   = nullptr;   // core-owned

    std::unique_ptr<SignalKClient>  client_;
    std::unique_ptr<SignalKDecoder> decoder_;

    QString host_;
    quint16 port_    = 80;
    bool    enabled_ = false;
};
