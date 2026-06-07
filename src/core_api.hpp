#pragma once
#include "plugin_api.hpp"
#include <memory>
#include <vector>

class NavDataStore;
class SideMenu;
class ChartView;
class QWidget;

// Concrete ICoreApi: routes plugin calls to the real core objects. Owned by the
// PluginManager; references (does not own) the core objects it bridges to.
class CoreApi : public ICoreApi {
public:
    CoreApi(NavDataStore* store, SideMenu* menu, ChartView* view, QWidget* dialogParent);

    INavDataPublisher*  navPublisher() override;
    const NavDataStore* navData() const override { return store_; }

    void addMenuAction(const QString& title, std::function<void()> onTriggered) override;
    void addMenuToggle(const QString& title, bool checked,
                       std::function<void(bool)> onToggled) override;

    IDataSource* registerDataSource(const QString& name,
                                    std::function<void()> onOpenSettings) override;

    void addChartOverlay(IChartOverlay* overlay) override;
    void removeChartOverlay(IChartOverlay* overlay) override;
    void requestChartRepaint() override;

    QWidget* dialogParent() override { return dialogParent_; }

private:
    NavDataStore* store_ = nullptr;
    SideMenu*     menu_ = nullptr;
    ChartView*    view_ = nullptr;
    QWidget*      dialogParent_ = nullptr;
    std::vector<std::unique_ptr<IDataSource>> dataSources_;   // owns handles
};
