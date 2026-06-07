#pragma once
#include <memory>
#include <vector>

class IPlugin;
class ICoreApi;

// Owns the registered plugins and drives their lifecycle against one ICoreApi.
// Built-in plugins are added before initializeAll(); dynamic loading would later
// add plugins discovered from disk here, behind the same interface.
class PluginManager {
public:
    explicit PluginManager(ICoreApi* core);
    ~PluginManager();

    // Take ownership of a plugin. Call before initializeAll().
    void add(std::unique_ptr<IPlugin> plugin);

    void initializeAll();   // initialize(core) each plugin, once
    void shutdownAll();     // shutdown() each plugin (idempotent)

private:
    ICoreApi* core_ = nullptr;
    std::vector<std::unique_ptr<IPlugin>> plugins_;
    bool initialized_ = false;
};
