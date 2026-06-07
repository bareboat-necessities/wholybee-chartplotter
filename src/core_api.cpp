#include "core_api.hpp"
#include "nav_data_store.hpp"
#include "side_menu.hpp"
#include "chart_view.hpp"

#include <QPushButton>

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
} // namespace

CoreApi::CoreApi(NavDataStore* store, SideMenu* menu, ChartView* view, QWidget* dialogParent)
    : store_(store), menu_(menu), view_(view), dialogParent_(dialogParent) {}

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

IDataSource* CoreApi::registerDataSource(const QString& name,
                                         std::function<void()> onOpenSettings) {
    QPushButton* item = menu_->addDataSourceItem(name, std::move(onOpenSettings));
    dataSources_.push_back(std::make_unique<DataSourceHandle>(menu_, item));
    return dataSources_.back().get();
}

void CoreApi::addChartOverlay(IChartOverlay* overlay)    { view_->addOverlay(overlay); }
void CoreApi::removeChartOverlay(IChartOverlay* overlay) { view_->removeOverlay(overlay); }
void CoreApi::requestChartRepaint()                      { view_->update(); }
