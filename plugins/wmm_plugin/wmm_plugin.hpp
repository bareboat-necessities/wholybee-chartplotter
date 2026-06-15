#pragma once
#include <QString>
#include <memory>
#include "plugin_api.hpp"
#include "wmm.hpp"

class QTimer;

// World Magnetic Model plugin. Every 4 seconds it reads the ownship latitude and
// longitude from the nav data store, computes the magnetic variation (declination)
// with the WMM2025 model, and publishes it back to the store with the source id
// "WMMplugin". The navigation/RMC code then uses this variation when no
// higher-priority source supplies one.
//
// Built as a separate DLL, loaded by the host at runtime via QPluginLoader (same
// path as the other plugins). Needs only Qt6::Core/Widgets — no extra deps.
class WmmPlugin : public IPlugin {
public:
    WmmPlugin();
    ~WmmPlugin() override;

    QString name() const override { return QStringLiteral("WMM Magnetic Variation"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

private:
    void computeAndPublish();

    ICoreApi*               core_ = nullptr;
    std::unique_ptr<QTimer> timer_;
    WmmModel                model_;
    bool                    logged_ = false;   // log the first computed value once
};
