#include "plugin_manager.hpp"
#include "plugin_api.hpp"

PluginManager::PluginManager(ICoreApi* core) : core_(core) {}

PluginManager::~PluginManager() { shutdownAll(); }

void PluginManager::add(std::unique_ptr<IPlugin> plugin) {
    plugins_.push_back(std::move(plugin));
}

void PluginManager::initializeAll() {
    if (initialized_) return;
    initialized_ = true;
    for (auto& p : plugins_) p->initialize(core_);
}

void PluginManager::shutdownAll() {
    if (!initialized_) return;
    initialized_ = false;
    for (auto& p : plugins_) p->shutdown();
}
