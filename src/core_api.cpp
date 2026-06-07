#include "core_api.hpp"
#include "nav_data_store.hpp"
#include "side_menu.hpp"
#include "chart_view.hpp"
#include "data_sources.hpp"

#include <QPushButton>
#include <QSettings>

namespace {
// Handle handed back to a plugin data source: drives its menu item's status dot.
class DataSourceHandle : public IDataSource {
public:
    DataSourceHandle(SideMenu* menu, QPushButton* item) : menu_(menu), item_(item) {}
    void setActive(bool on) override { if (menu_) menu_->setItemDot(item_, on); }
private:
    SideMenu*    menu_ = nullptr;
    QPushButton* item_ = nullptr;
};

// Per-plugin persistent settings, namespaced under plugins/<id>/ in QSettings.
class PluginSettings : public IPluginSettings {
public:
    explicit PluginSettings(const QString& pluginId)
        : prefix_(QStringLiteral("plugins/") + pluginId + QLatin1Char('/')) {}
    void setValue(const QString& key, const QVariant& value) override {
        QSettings().setValue(prefix_ + key, value);
    }
    QVariant value(const QString& key, const QVariant& def) const override {
        return QSettings().value(prefix_ + key, def);
    }
private:
    QString prefix_;
};
} // namespace

CoreApi::CoreApi(NavDataStore* store, SideMenu* menu, ChartView* view,
                 DataSourceRegistry* registry, QWidget* dialogParent)
    : store_(store), menu_(menu), view_(view), registry_(registry),
      dialogParent_(dialogParent) {}

CoreApi::~CoreApi() = default;

INavDataPublisher* CoreApi::navPublisher() {
    return store_;   // NavDataStore implements INavDataPublisher
}

void CoreApi::addMenuAction(const QString& title, std::function<void()> onTriggered) {
    menu_->addPluginAction(title, std::move(onTriggered));
}

void CoreApi::addMenuToggle(const QString& title, bool checked,
                            std::function<void(bool)> onToggled) {
    menu_->addPluginToggle(title, checked, std::move(onToggled));
}

IPluginSettings* CoreApi::pluginSettings(const QString& pluginId) {
    auto it = pluginSettings_.find(pluginId);
    if (it == pluginSettings_.end())
        it = pluginSettings_.emplace(pluginId,
                                     std::make_unique<PluginSettings>(pluginId)).first;
    return it->second.get();
}

IDataSource* CoreApi::registerDataSource(const QString& sourceId, const QString& name,
                                         std::function<void()> onOpenSettings) {
    QPushButton* item = menu_->addDataSourceItem(name, std::move(onOpenSettings));
    if (registry_) registry_->add(sourceId, name);   // joins Data Priority
    dataSources_.push_back(std::make_unique<DataSourceHandle>(menu_, item));
    return dataSources_.back().get();
}

void CoreApi::addChartOverlay(IChartOverlay* overlay)    { view_->addOverlay(overlay); }
void CoreApi::removeChartOverlay(IChartOverlay* overlay) { view_->removeOverlay(overlay); }
void CoreApi::requestChartRepaint()                      { view_->update(); }
