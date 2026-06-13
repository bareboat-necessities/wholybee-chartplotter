#pragma once
#include <QObject>
#include <QtPlugin>
#include <memory>

class IPlugin;

// Plugin ABI version. Bumped whenever a virtual method on ICoreApi/IPlugin or
// the layout of a type crossing the boundary changes. Plugins compiled against
// a different version are rejected by the loader before any vtable touch.
inline constexpr int kPluginAbiVersion = 1;

// Q_DECLARE_INTERFACE IID. Must match the IID in each plugin's
// Q_PLUGIN_METADATA(); change in lock-step with kPluginAbiVersion.
#define CHARTPLOTTER_PLUGIN_IID "com.chartplotter.IPluginFactory/1.0"

// The single symbol a dynamic plugin DLL exposes to the host. The plugin's
// shared library contains exactly one QObject implementing this interface; the
// host instantiates that QObject through QPluginLoader, calls abiVersion() as
// a defence-in-depth check (the metadata is checked first, without mapping the
// binary), and then asks the factory to construct the actual IPlugin.
//
// Keeping the factory separate from the IPlugin lets plugins keep their main
// classes free of QObject inheritance — most of our existing plugins (NMEA
// 0183/2000, MBTiles etc.) inherit from IPlugin and ISettingsPageProvider and
// would otherwise need a diamond.
class IPluginFactory {
public:
    virtual ~IPluginFactory() = default;
    virtual int abiVersion() const = 0;
    virtual std::unique_ptr<IPlugin> create() = 0;
};

Q_DECLARE_INTERFACE(IPluginFactory, CHARTPLOTTER_PLUGIN_IID)
