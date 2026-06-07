#pragma once
#include <QString>
#include <QPointer>
#include <memory>
#include "plugin_api.hpp"
#include "nmea0183_client.hpp"   // NmeaTransport, Nmea0183Client

class Nmea0183DebugWindow;
class AisDecoder;

// NMEA 0183 network source, as a built-in plugin on the plugin API. Owns the
// network client and (lazily) the raw-data debug window; registers itself as a
// data source (Data Connections item + green decoding dot + Data Priority), and
// provides a core-hosted settings page for the TCP/UDP connection config.
// Connection settings persist via the plugin settings store.
class Nmea0183Plugin : public IPlugin, public ISettingsPageProvider {
public:
    Nmea0183Plugin();
    ~Nmea0183Plugin() override;

    QString name() const override { return QStringLiteral("NMEA 0183"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

    // ISettingsPageProvider
    QString  settingsPageTitle() const override { return QStringLiteral("NMEA 0183"); }
    QWidget* createSettingsPage(QWidget* parent) override;

private:
    void loadConfig();   // from plugin settings (migrating old core keys once)
    void applyConfig(NmeaTransport transport, const QString& host,
                     quint16 port, bool enabled);
    void showDebugWindow();

    ICoreApi*        core_ = nullptr;
    IDataSource*     dataSource_ = nullptr;   // owned by the core
    IPluginSettings* settings_ = nullptr;     // owned by the core
    std::unique_ptr<Nmea0183Client>  client_;
    std::unique_ptr<AisDecoder>      ais_;    // decodes !AIVDM/!AIVDO -> AIS store
    QPointer<Nmea0183DebugWindow>    debug_;  // parented to the main window

    NmeaTransport transport_ = NmeaTransport::Tcp;
    QString       host_;
    quint16       port_ = 10110;
    bool          enabled_ = false;
};
