#pragma once
#include <QString>
#include <memory>
#include "plugin_api.hpp"
#include "nmea2000_client.hpp"   // N2kTransport, N2kFormat, Nmea2000Client

class N2kNavSender;

// NMEA 2000 network source, as a built-in plugin on the plugin API. Mirrors
// Nmea0183Plugin: owns the network client, registers itself as a data source
// (Data Connections item + green decoding dot + Data Priority), and provides
// a core-hosted settings page for the connection config.
//
// Today only Actisense "N2K ASCII" output is supported; the settings page has
// a format radio group with room for additional formats to slot in alongside.
// Connection settings persist via the plugin settings store.
class Nmea2000Plugin : public IPlugin, public ISettingsPageProvider {
public:
    Nmea2000Plugin();
    ~Nmea2000Plugin() override;

    QString name() const override { return QStringLiteral("NMEA 2000"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

    QString  settingsPageTitle() const override { return QStringLiteral("NMEA 2000"); }
    QWidget* createSettingsPage(QWidget* parent) override;

private:
    void loadConfig();
    void applyConfig(N2kTransport transport, N2kFormat format,
                     const QString& host, quint16 port, bool enabled);

    ICoreApi*        core_ = nullptr;
    IDataSource*     dataSource_ = nullptr;
    IPluginSettings* settings_ = nullptr;
    std::unique_ptr<Nmea2000Client> client_;
    std::unique_ptr<N2kNavSender>   navSender_;

    N2kTransport transport_ = N2kTransport::Tcp;
    N2kFormat    format_    = N2kFormat::ActisenseAscii;
    QString      host_;
    quint16      port_ = 2598;        // common default for W2K N2K ASCII
    bool         enabled_ = false;
};
