# Plugin API

The boundary between the core application and plugins (Milestone 3 in
`ProjectSpec.md`). Plugins extend the app — communication, instruments, AIS,
routing, overlays — without touching core internals. They publish data, subscribe
to data, contribute UI, and draw overlays through controlled APIs; the core owns
the shared data models, the chart canvas, settings, and object lifetimes.

This is deliberately **built-in plugins first**: the same interfaces a dynamic
(DLL/SO) plugin will use later, exercised by in-process plugins so the contract
can be refined before ABI stability, versioning, and platform loaders matter.

```
+-----------+   initialize(core)   +-----------+   bridges to   +---------------+
|  plugin   | <------------------- | PluginMgr |                | core objects  |
| (IPlugin) |                      |           |   ICoreApi     | NavDataStore  |
|           | --- publish/draw --> |  CoreApi  | -------------> | ChartView     |
|           | <-- subscribe ------ |           |                | SideMenu      |
+-----------+                      +-----------+                +---------------+
```

## Lifecycle

```cpp
class IPlugin {
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual void initialize(ICoreApi* core) = 0;   // register contributions
    virtual void shutdown() = 0;                    // release them
};
```

`PluginManager` owns the plugins and drives them against one `ICoreApi`:

```cpp
PluginManager mgr(coreApi);
mgr.add(std::make_unique<TestPlugin>());   // built-in; dynamic discovery later
mgr.initializeAll();                       // initialize(core) each, once
// ... app runs ...
mgr.shutdownAll();                         // shutdown() each (idempotent)
```

A plugin does all its wiring in `initialize()` — grabs core handles, registers
menu items / overlays / data sources — and undoes anything that outlives it in
`shutdown()` (e.g. removing an overlay it registered).

## Core services: `ICoreApi`

The single, stable surface handed to a plugin. The core implements it (`CoreApi`)
and routes calls to the real objects.

```cpp
class ICoreApi {
    // Navigation data
    INavDataPublisher*  navPublisher();        // publish updates
    const NavDataStore* navData() const;       // read state; connect ownshipChanged()

    // Menu contributions (main menu "Plugins" section)
    void addMenuAction(QString title, std::function<void()> onTriggered);
    void addMenuToggle(QString title, bool checked, std::function<void(bool)> onToggled);

    // Per-plugin persistent settings
    IPluginSettings* pluginSettings(QString pluginId);

    // Settings pages (Settings > Plugin Settings)
    void addSettingsPage(ISettingsPageProvider* provider);
    void showSettingsPage(ISettingsPageProvider* provider);

    // Data sources (Settings > Data Connections; also joins Data Priority)
    IDataSource* registerDataSource(QString sourceId, QString name,
                                    std::function<void()> onOpenSettings);

    // Chart overlays
    void addChartOverlay(IChartOverlay* overlay);
    void removeChartOverlay(IChartOverlay* overlay);
    void requestChartRepaint();

    QWidget* dialogParent();                    // parent for plugin dialogs
};
```

### Navigation data

Plugins **publish** through `INavDataPublisher` (same contract the simulator and
NMEA client use), so per-value source, timestamp, aging, and priority
arbitration all apply automatically — see `docs/nav_data_store.md`.

```cpp
NavValueMeta m;
m.source = QStringLiteral("my-plugin");
m.timestampUtc = QDateTime::currentDateTimeUtc();
core->navPublisher()->publishDepth(metres, m);
```

To **read / subscribe**, use `navData()` (a `const NavDataStore*`) and connect to
its `ownshipChanged()` signal:

```cpp
connect(core->navData(), &NavDataStore::ownshipChanged, this, [this] {
    const NavValue& d = store_->ownship().depthMeters;
    if (d.valid()) show(d.value, d.source);   // value + provenance + freshness
});
```

### Menu contributions

`addMenuAction` / `addMenuToggle` append to a **Plugins** section of the main menu
(hidden until the first item is added). The toggle uses the same check-mark style
as the built-in toggles. Callbacks fire on the GUI thread.

### Data sources

`registerDataSource(sourceId, name, onOpenSettings)` makes the plugin a
first-class navigation source:

- The core adds an item named `name` under **Settings > Data Connections**, with
  a status dot, sitting alongside NMEA 0183 and Simulator.
- `sourceId` (the stable id the plugin stamps on `NavValueMeta.source`) is added
  to the runtime `DataSourceRegistry`, so the source **appears in the Data
  Priority dialog** and participates in priority/fall-back arbitration like a
  built-in source.
- Clicking the item invokes `onOpenSettings` — typically the plugin's own
  settings dialog.
- The returned `IDataSource*` lets the plugin drive its dot:

```cpp
class IDataSource { virtual void setActive(bool on) = 0; };   // green dot on/off
```

The core owns the handle; the plugin holds a non-owning pointer.

### Persistent settings

`pluginSettings(pluginId)` returns an `IPluginSettings` namespaced to the plugin,
backed by the same store the core uses (survives restarts):

```cpp
class IPluginSettings {
    void     setValue(const QString& key, const QVariant& value);
    QVariant value(const QString& key, const QVariant& def = {}) const;
};
```

Values are stored under `plugins/<pluginId>/<key>`. The Test Plugin uses this to
remember whether it is enabled as a data source and restores it on the next run.

### Settings pages

A plugin supplies only the **content** of its settings page; the core hosts it
(window chrome, title, parenting, single-instance) so plugins don't manage their
own dialogs:

```cpp
class ISettingsPageProvider {
    QString  settingsPageTitle() const;
    QWidget* createSettingsPage(QWidget* parent);   // core takes ownership
};
```

`addSettingsPage(provider)` adds an item under **Settings > Plugin Settings**
(hidden until the first one). `showSettingsPage(provider)` opens it on demand —
e.g. a data source routes its item's click here, so the same page serves both
entry points. The Test Plugin implements this; its page is the single
"Enable as data source" checkbox.

## Chart overlays

Plugins do not add `QGraphicsItem`s or know how the canvas is implemented. They
implement `IChartOverlay` and register it; the core calls `paint()` each frame
after its own drawing, in device coordinates.

```cpp
class IChartOverlay {
    virtual void paint(QPainter& painter, const ChartViewport& viewport) = 0;
    virtual bool hitTest(const QPointF& screenPt) { return false; }   // optional
};
```

`ChartViewport` is a per-frame snapshot of the camera with the coordinate helpers
an overlay needs — so drawing can be expressed geographically without knowing the
projection or zoom:

```cpp
struct ChartViewport {
    QPointF geoToScreen(double latDeg, double lonDeg) const;   // handles 180 wrap
    void    screenToGeo(const QPointF& px, double& latDeg, double& lonDeg) const;
    double  pixelsPerMetre() const;
    QSize   viewportSize() const;
};
```

The viewport is valid only for the duration of one `paint()` call. The core owns
z-order (registration order) and lifetime; the plugin owns the overlay object and
must `removeChartOverlay()` it in `shutdown()`.

## Worked example: the Test Plugin

`TestPlugin` (built-in) exercises the whole surface:

| Contribution | API used | What it does |
|--------------|----------|--------------|
| **"Hello World"** toggle (main menu) | `addMenuToggle` + `addChartOverlay` | Toggles an overlay drawing "Hello World", anchored to the ownship via `geoToScreen` (falls back to viewport centre). |
| **"Publish Depth…"** action (main menu) | `addMenuAction`, `navData`, `navPublisher` | Opens a dialog showing current depth (value / source / age, live, greyed when stale) and publishes a new depth value. |
| **"Test Plugin"** data source (Data Connections) | `registerDataSource`, `IDataSource::setActive`, `navPublisher` | Drives its green dot; while enabled publishes a varying depth at 1 Hz (source `test-plugin`). Joins Data Priority. |
| **"Test Plugin"** settings page (Plugin Settings) | `ISettingsPageProvider`, `addSettingsPage`, `pluginSettings` | Core-hosted page with one "Enable as data source" checkbox; the enabled state persists across runs. The data-source item opens this same page. |

Wiring it up in the core is three lines (`MainWindow`):

```cpp
coreApi_ = std::make_unique<CoreApi>(navStore_, sideMenu_, view_, &registry_, this);
plugins_ = std::make_unique<PluginManager>(coreApi_.get());
plugins_->add(std::make_unique<TestPlugin>());
plugins_->initializeAll();
```

## Extending: how to add a plugin

1. Implement `IPlugin`. In `initialize(core)`, register your contributions:
   menu items, overlays, and/or a data source. Stash `core` and any handles.
2. Publish nav updates through `core->navPublisher()`; read/subscribe via
   `core->navData()`.
3. Draw with an `IChartOverlay` and the `ChartViewport` helpers.
4. Undo anything that outlives the plugin in `shutdown()`.
5. Register it with the `PluginManager` (today: `mgr.add(...)` in `MainWindow`).

## Extensibility — honest assessment

Strong:

- The publish / subscribe / contribute-UI / draw-overlay boundary from the spec
  is real and enforced — plugins never reach into the chart scene, nav structs,
  or settings directly.
- Built-in plugins use the exact interfaces a dynamic plugin will, so the
  contract is being shaken out before ABI concerns appear.
- Nav publishing rides the existing per-value source / aging / priority model,
  and plugin sources join the runtime `DataSourceRegistry`, so they appear in the
  Data Priority dialog and arbitrate alongside built-in sources.
- Plugins persist their own settings via `pluginSettings(pluginId)`, namespaced
  and backed by the core store, and contribute a core-hosted settings page via
  `ISettingsPageProvider` (the plugin supplies only the content widget).

Where it will grow (additions, not rewrites):

- **Dynamic loading.** No DLL/SO discovery, versioning, or ABI freeze yet. The
  `PluginManager` is the seam — it would enumerate plugins from disk and create
  them behind `IPlugin`, with everything downstream unchanged.

- **More contribution points.** `IChartProvider` (chart formats), `IAisProvider`,
  `IInstrumentProvider`, `IRouteTool`, and overlay `hitTest` routing are sketched
  in the spec and slot in as further `ICoreApi` services / interfaces.

- **Threading.** Today plugins run on the GUI thread. A source doing blocking I/O
  would need its own thread and to marshal publishes back — a documentation/helper
  concern, not an interface change.

None of these breaks the `IPlugin` / `ICoreApi` contract or existing plugins.
