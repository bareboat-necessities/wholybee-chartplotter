#include "gpx_plugin.hpp"
#include "plugin_factory.hpp"

#include <QObject>

// QPluginLoader entry point for the GPX Plugin DLL.
//
// The factory is the only QObject the host's QPluginLoader instantiates; it
// hands back the actual IPlugin (a plain C++ class, free of QObject inheritance)
// on demand. Keeping the factory thin means the real work doesn't run until the
// host has accepted the ABI / metadata.
class GpxPluginFactory : public QObject, public IPluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID CHARTPLOTTER_PLUGIN_IID FILE "gpx_plugin.json")
    Q_INTERFACES(IPluginFactory)
public:
    int abiVersion() const override { return kPluginAbiVersion; }
    std::unique_ptr<IPlugin> create() override {
        return std::make_unique<GpxPlugin>();
    }
};

// Required when Q_OBJECT is declared in a .cpp file — AUTOMOC generates this
// next to the source so the meta-object code is included in this translation
// unit.
#include "gpx_plugin_factory.moc"
