#pragma once
#include <QWidget>
#include <QStringList>
#include <QSet>

class InstrumentsPlugin;
class QVBoxLayout;

// Content of the Instruments settings page (hosted by the core under Settings >
// Plugin Settings). Lets the user pick the bar orientation and scale, and choose
// which catalogue instruments appear and in what order. Every change is applied
// to the live bar and persisted immediately — there is no separate Apply button,
// matching the touch-first, instantly-reflected style of the rest of the app.
class InstrumentsSettingsPage : public QWidget {
    Q_OBJECT
public:
    InstrumentsSettingsPage(InstrumentsPlugin* plugin, QWidget* parent);

private:
    void rebuildList();
    void moveRow(int index, int delta);
    void pushToPlugin();           // send current order + selection to the plugin

    InstrumentsPlugin* plugin_ = nullptr;
    QStringList   order_;          // working display order of every catalogue id
    QSet<QString> selected_;       // working set of included ids
    QWidget*      rowContainer_ = nullptr;
    QVBoxLayout*  rowLayout_ = nullptr;
};
