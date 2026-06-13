#include "plugin_manager.hpp"
#include "plugin_api.hpp"
#include "plugin_factory.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QPluginLoader>
#include <QDebug>

PluginManager::PluginManager(ICoreApi* core) : core_(core) {}

// Out-of-line: ~Entry needs IPlugin and QPluginLoader complete; both are
// only forward-declared in the header.
PluginManager::~PluginManager() { shutdownAll(); }

void PluginManager::add(std::unique_ptr<IPlugin> plugin) {
    plugins_.push_back({ std::move(plugin), nullptr });
}

void PluginManager::loadFromDirectory(const QString& dir) {
    QDir d(dir);
    if (!d.exists()) {
        qInfo() << "Plugin directory does not exist (skipping):" << dir;
        return;
    }
    // Cross-platform suffix list; entryInfoList filters case-insensitively on
    // Windows, so .DLL works too.
    const QStringList filters = {
        QStringLiteral("*.dll"),
        QStringLiteral("*.so"),
        QStringLiteral("*.dylib"),
    };
    const auto files = d.entryInfoList(filters, QDir::Files);
    for (const QFileInfo& fi : files) {
        auto loader = std::make_unique<QPluginLoader>(fi.absoluteFilePath());

        // Check metadata without mapping the binary's code: rejected plugins
        // never get to run their static constructors.
        const QJsonObject meta = loader->metaData()
                                     .value(QStringLiteral("MetaData")).toObject();
        if (meta.isEmpty()) {
            // Not one of ours (or any Qt plugin); ignore silently.
            continue;
        }
        const int api = meta.value(QStringLiteral("apiVersion")).toInt(-1);
        if (api != kPluginAbiVersion) {
            qWarning() << "Plugin" << fi.fileName()
                       << "apiVersion" << api
                       << "does not match host" << kPluginAbiVersion << "(skipping)";
            continue;
        }

        QObject* obj = loader->instance();
        if (!obj) {
            qWarning() << "Failed to load" << fi.fileName() << ":"
                       << loader->errorString();
            continue;
        }
        auto* factory = qobject_cast<IPluginFactory*>(obj);
        if (!factory) {
            qWarning() << fi.fileName()
                       << "does not implement IPluginFactory (wrong IID?)";
            continue;
        }
        if (factory->abiVersion() != kPluginAbiVersion) {
            qWarning() << fi.fileName() << "ABI mismatch at runtime:"
                       << "plugin reports" << factory->abiVersion()
                       << "host" << kPluginAbiVersion;
            continue;
        }

        auto plugin = factory->create();
        if (!plugin) {
            qWarning() << fi.fileName() << "factory->create() returned null";
            continue;
        }

        const QString name = meta.value(QStringLiteral("name")).toString(fi.fileName());
        qInfo() << "Loaded plugin:" << name << "from" << fi.fileName();
        plugins_.push_back({ std::move(plugin), std::move(loader) });
    }
}

void PluginManager::initializeAll() {
    if (initialized_) return;
    initialized_ = true;
    for (auto& e : plugins_) e.plugin->initialize(core_);
}

void PluginManager::shutdownAll() {
    if (!initialized_) return;
    initialized_ = false;
    // Reverse order: a plugin that registered something with the core should
    // tear down before any earlier plugin it might have depended on.
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it)
        it->plugin->shutdown();
}
