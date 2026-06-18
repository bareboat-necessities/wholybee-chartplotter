#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include "plugin_api.hpp"
#include "instrument_config.hpp"

class InstrumentBar;

// Instruments plugin: shows a configurable bar of instrument tiles over the
// chart, each displaying a value from the nav data store as a digital read-out
// or an analogue gauge. Built as a runtime DLL loaded via QPluginLoader, like
// the other plugins under plugins/.
//
// Contributions:
//   * an "Instruments" toggle in the main menu's Plugins section (show / hide);
//   * an "Instruments" page under Settings > Plugin Settings to choose the
//     orientation, scale, and which instruments appear (and in what order).
//
// The set of available instruments comes from a user-editable instruments.xml
// (see InstrumentCatalog). Selection, order, orientation, scale, visibility and
// bar position all persist via IPluginSettings.
class InstrumentsPlugin : public IPlugin, public ISettingsPageProvider {
public:
    InstrumentsPlugin();
    ~InstrumentsPlugin() override;

    QString name() const override { return QStringLiteral("Instruments"); }
    QString version() const override { return QStringLiteral("1.0"); }
    void initialize(ICoreApi* core) override;
    void shutdown() override;

    // ISettingsPageProvider
    QString  settingsPageTitle() const override { return QStringLiteral("Instruments"); }
    QWidget* createSettingsPage(QWidget* parent) override;

    // ---- surface consumed by the settings page -----------------------------
    const QList<InstrumentDef>& catalog() const { return catalog_; }
    QStringList order() const    { return order_; }
    QStringList selected() const { return selected_; }
    bool   isHorizontal() const  { return horizontal_; }
    double scale() const         { return scale_; }

    // Apply (and persist) a new ordering / selection of instruments.
    void applyInstruments(const QStringList& order, const QStringList& selected);
    void setHorizontal(bool horizontal);
    void setScale(double scale);

private:
    void setEnabled(bool on);
    void reconcileWithCatalog();   // align saved order/selection to the catalogue
    QList<InstrumentDef> selectedDefs() const;   // chosen defs, in order
    QWidget* chartParentWidget() const;
    void rebuildBar();
    void loadConfig();

    ICoreApi*        core_     = nullptr;
    IPluginSettings* settings_ = nullptr;   // core-owned, persisted

    QList<InstrumentDef> catalog_;          // all available instruments
    QStringList order_;                     // display order of every catalogue id
    QStringList selected_;                  // included ids, in bar order
    bool   horizontal_ = true;
    double scale_      = 1.0;
    bool   enabled_    = false;

    std::unique_ptr<InstrumentBar> bar_;
};
