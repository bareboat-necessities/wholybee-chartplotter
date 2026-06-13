#pragma once
#include <QString>
#include <memory>
#include <vector>

class IPlugin;
class ICoreApi;
class QPluginLoader;

// Owns the registered plugins and drives their lifecycle against one ICoreApi.
// Built-in plugins are still added via add(); dynamic plugins discovered on
// disk are loaded via loadFromDirectory() and live alongside built-ins,
// indistinguishable to the rest of the host.
class PluginManager {
public:
    explicit PluginManager(ICoreApi* core);
    ~PluginManager();

    // Take ownership of a built-in plugin. Call before initializeAll().
    void add(std::unique_ptr<IPlugin> plugin);

    // Scan a directory for shared-library plugins (*.dll / *.so / *.dylib).
    // Each candidate is validated against kPluginAbiVersion using both the
    // JSON metadata (cheap, no binary mapped) and a defence-in-depth virtual
    // call after the factory is instantiated. Plugins that fail validation
    // are skipped with a warning; the call never fails the whole scan.
    void loadFromDirectory(const QString& dir);

    void initializeAll();   // initialize(core) each plugin, once
    void shutdownAll();     // shutdown() each plugin (idempotent)

private:
    // A loaded plugin and the QPluginLoader that keeps its DLL mapped (null
    // for built-in plugins). Order of destruction matters: shutdown_->plugin_
    // -> loader_, so shutdown() always runs against a live vtable.
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        std::unique_ptr<QPluginLoader> loader;   // null for built-in
    };

    ICoreApi* core_ = nullptr;
    std::vector<Entry> plugins_;
    bool initialized_ = false;
};
