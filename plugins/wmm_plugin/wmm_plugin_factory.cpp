#include "wmm_plugin.hpp"
#include "plugin_factory.hpp"

#include <QObject>

// QPluginLoader entry point for the WMM Plugin DLL. The factory is the only
// QObject the host instantiates; it hands back the actual IPlugin on demand,
// after the host has accepted the ABI / metadata.
class WmmPluginFactory : public QObject, public IPluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID CHARTPLOTTER_PLUGIN_IID FILE "wmm_plugin.json")
    Q_INTERFACES(IPluginFactory)
public:
    int abiVersion() const override { return kPluginAbiVersion; }
    std::unique_ptr<IPlugin> create() override {
        return std::make_unique<WmmPlugin>();
    }
};

#include "wmm_plugin_factory.moc"
