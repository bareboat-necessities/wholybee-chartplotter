#pragma once
#include <QString>
#include "plugin_api.hpp"

// GPX import/export plugin. Adds a single "GPX Import / Export…" item to the
// Plugins menu that opens a touch-friendly dialog for reading and writing GPX
// files against the core's routes database (ICoreApi::routes()).
//
// Built as a separate DLL, loaded by the host at runtime via QPluginLoader (same
// path as plugins/signalk_plugin). Needs only Qt6::Widgets/Core — GPX is parsed
// with QXmlStreamReader/Writer, so there are no extra runtime dependencies.
class GpxPlugin : public IPlugin {
public:
    GpxPlugin();
    ~GpxPlugin() override;

    QString name() const override { return QStringLiteral("GPX Import / Export"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

private:
    void openDialog();

    ICoreApi* core_ = nullptr;
};
