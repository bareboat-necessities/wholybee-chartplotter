#include "instruments_plugin.hpp"
#include "instrument_bar.hpp"
#include "instruments_settings_page.hpp"

#include <QMainWindow>
#include <QPoint>

namespace {
const QString kPluginId = QStringLiteral("instruments");

// Sensible starter selection on a fresh install: the four common digital
// read-outs, in a natural order. Only ids that exist in the catalogue survive
// reconciliation, so this is safe even with a customised XML.
const QStringList kDefaultSelected = {
    QStringLiteral("sog-digital"),
    QStringLiteral("cog-digital"),
    QStringLiteral("stw-digital"),
    QStringLiteral("aws-digital"),
};
}  // namespace

InstrumentsPlugin::InstrumentsPlugin() = default;
InstrumentsPlugin::~InstrumentsPlugin() = default;

void InstrumentsPlugin::initialize(ICoreApi* core) {
    core_     = core;
    settings_ = core_->pluginSettings(kPluginId);

    catalog_ = InstrumentCatalog::load();
    loadConfig();
    reconcileWithCatalog();

    // Floating bar over the chart. Parented to the chart area so it overlays the
    // chart (and repositions with it), like the core's nav display window.
    bar_ = std::make_unique<InstrumentBar>(core_->navData(), chartParentWidget());
    bar_->setHorizontal(horizontal_);
    bar_->setScale(scale_);
    const QPoint saved(settings_->value(QStringLiteral("posX"), -1).toInt(),
                       settings_->value(QStringLiteral("posY"), -1).toInt());
    bar_->restorePosition(saved);
    QObject::connect(bar_.get(), &InstrumentBar::positionChanged, bar_.get(),
                     [this](QPoint p) {
        if (!settings_) return;
        settings_->setValue(QStringLiteral("posX"), p.x());
        settings_->setValue(QStringLiteral("posY"), p.y());
    });
    rebuildBar();
    bar_->setVisible(enabled_);

    // Main-menu Plugins toggle: show / hide the bar.
    core_->addMenuToggle(QStringLiteral("Instruments"), enabled_,
                         [this](bool on) { setEnabled(on); });

    // Settings page under Settings > Plugin Settings.
    core_->addSettingsPage(this);
}

void InstrumentsPlugin::shutdown() {
    bar_.reset();        // removes the bar from the chart view before unload
    core_ = nullptr;
}

QWidget* InstrumentsPlugin::createSettingsPage(QWidget* parent) {
    return new InstrumentsSettingsPage(this, parent);
}

void InstrumentsPlugin::loadConfig() {
    horizontal_ = settings_->value(QStringLiteral("orientation"),
                                   QStringLiteral("horizontal")).toString()
                  != QStringLiteral("vertical");
    scale_   = settings_->value(QStringLiteral("scale"), 1.0).toDouble();
    if (scale_ < 0.5 || scale_ > 3.0) scale_ = 1.0;
    enabled_ = settings_->value(QStringLiteral("enabled"), false).toBool();
    order_    = settings_->value(QStringLiteral("order")).toStringList();
    selected_ = settings_->value(QStringLiteral("selected")).toStringList();
}

// Align the saved order/selection to the current catalogue: keep saved ids that
// still exist (in their saved order), append any newly added ones, and drop any
// that vanished. On a fresh install (no saved order) fall back to catalogue order
// and the default selection.
void InstrumentsPlugin::reconcileWithCatalog() {
    QStringList catalogIds;
    for (const InstrumentDef& d : catalog_) catalogIds.push_back(d.id);

    const bool firstRun = order_.isEmpty() && selected_.isEmpty()
                          && !settings_->value(QStringLiteral("order")).isValid();

    QStringList newOrder;
    for (const QString& id : order_)
        if (catalogIds.contains(id) && !newOrder.contains(id)) newOrder.push_back(id);
    for (const QString& id : catalogIds)
        if (!newOrder.contains(id)) newOrder.push_back(id);   // newly added instruments
    order_ = newOrder;

    if (firstRun) {
        selected_.clear();
        for (const QString& id : kDefaultSelected)
            if (catalogIds.contains(id)) selected_.push_back(id);
    } else {
        QStringList keep;
        for (const QString& id : selected_)
            if (catalogIds.contains(id)) keep.push_back(id);
        selected_ = keep;
    }

    // Persist the reconciled lists so the file reflects what is actually in use.
    settings_->setValue(QStringLiteral("order"), order_);
    settings_->setValue(QStringLiteral("selected"), selected_);
}

QList<InstrumentDef> InstrumentsPlugin::selectedDefs() const {
    QList<InstrumentDef> out;
    // selected_ already holds the included ids in display order.
    for (const QString& id : selected_)
        for (const InstrumentDef& d : catalog_)
            if (d.id == id) { out.push_back(d); break; }
    return out;
}

void InstrumentsPlugin::rebuildBar() {
    if (bar_) bar_->setInstruments(selectedDefs());
}

void InstrumentsPlugin::applyInstruments(const QStringList& order,
                                         const QStringList& selected) {
    order_    = order;
    selected_ = selected;
    if (settings_) {
        settings_->setValue(QStringLiteral("order"), order_);
        settings_->setValue(QStringLiteral("selected"), selected_);
    }
    rebuildBar();
}

void InstrumentsPlugin::setHorizontal(bool horizontal) {
    horizontal_ = horizontal;
    if (settings_)
        settings_->setValue(QStringLiteral("orientation"),
                            horizontal ? QStringLiteral("horizontal")
                                       : QStringLiteral("vertical"));
    if (bar_) bar_->setHorizontal(horizontal);
}

void InstrumentsPlugin::setScale(double scale) {
    scale_ = scale;
    if (settings_) settings_->setValue(QStringLiteral("scale"), scale_);
    if (bar_) bar_->setScale(scale_);
}

void InstrumentsPlugin::setEnabled(bool on) {
    enabled_ = on;
    if (settings_) settings_->setValue(QStringLiteral("enabled"), on);
    if (bar_) bar_->setVisible(on);
}

QWidget* InstrumentsPlugin::chartParentWidget() const {
    QWidget* p = core_ ? core_->dialogParent() : nullptr;
    // Prefer the main window's central widget (the chart view) so the bar
    // overlays the chart rather than the whole window chrome / status bar.
    if (auto* mw = qobject_cast<QMainWindow*>(p))
        if (QWidget* central = mw->centralWidget()) return central;
    return p;
}
