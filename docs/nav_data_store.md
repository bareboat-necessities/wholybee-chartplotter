# NavDataStore

The core navigation data store: the single source of truth for live navigation
state. Sources publish updates through a stable interface; consumers subscribe
to signals; the core owns the data.

This is the foundation everything navigational eventually hangs off (instruments,
AIS, routing, autopilot). It implements the publish/subscribe contract in
`ProjectSpec.md`: plugins never mutate shared state directly — they call into a
stable publisher API, and the core decides what's authoritative.

```
+----------+     publish      +--------------+     signal      +-----------+
| sources  | ---------------> | NavDataStore | --------------> | consumers |
| sim,     |                  |              |                 | chart     |
| NMEA0183,|                  | OwnshipState |                 | view,     |
| NMEA2000,|                  | (per-value   |                 | NavData   |
| Signal K |                  |  source +    |                 | browser,  |
| ...      |                  |  freshness)  |                 | future    |
+----------+                  +--------------+                 +-----------+
```

## Data model

Each navigation value is **self-describing**: it carries its own source and
timestamp and ages independently. This is what lets position come from NMEA 0183
while depth and wind arrive from NMEA 2000, each with its own freshness.

### `NavValueMeta`

Provenance a publisher supplies when it sets a value:

| field          | meaning                                                |
|----------------|--------------------------------------------------------|
| `source`       | e.g. `"simulator"`, `"nmea0183"`, `"nmea2000"`         |
| `timestampUtc` | when the source produced this value (UTC)              |

### `NavValue`

A single value plus the freshness the store derives for it:

```cpp
struct NavValue {
    double       value;
    QString      source;        // empty until first set
    QDateTime    timestampUtc;  // invalid until first set
    double       ageSeconds;    // maintained by the store's tick
    NavFreshness freshness;     // Fresh / Stale / Invalid

    bool   valid() const;                 // freshness != Invalid
    bool   stale() const;                 // freshness == Stale
    double valueOr(double fallback) const;
};
```

A value is "available" when `valid()` — i.e. it has been set and has not aged
out. A never-set value and an aged-out value both read as Invalid, so consumers
treat them identically.

### `OwnshipState`

Live ownship navigation as a flat struct of `NavValue`s. Names mirror Signal K
paths conceptually (`navigation.position`, `navigation.courseOverGroundTrue`, …)
but storage is typed C++, not a JSON tree.

```cpp
NavValue latitudeDeg;
NavValue longitudeDeg;
NavValue cogDegTrue;
NavValue sogKnots;
NavValue headingDegTrue;
NavValue headingDegMag;
NavValue variationDeg;
NavValue depthMeters;
NavValue windSpeedKnots;
NavValue windAngleDeg;
```

### `NavFreshness`

Per-value state machine reflecting how stale that value is:

| state     | when                                  |
|-----------|---------------------------------------|
| `Fresh`   | `age < staleSeconds`                  |
| `Stale`   | `staleSeconds ≤ age < invalidSeconds` |
| `Invalid` | `age ≥ invalidSeconds` (or never set) |

Thresholds are runtime-configurable via `setStaleSeconds()` /
`setInvalidSeconds()`; defaults are 5 s and 30 s. They apply to every value.

## Publisher API

Sources call into the store through `INavDataPublisher`. Each call carries its
own `meta`, so different fields can originate from different sources and age
independently:

```cpp
class INavDataPublisher {
    virtual void publishOwnshipPosition(double latDeg, double lonDeg,
                                        const NavValueMeta& meta) = 0;
    virtual void publishCogSog(double cogDegTrue, double sogKnots,
                               const NavValueMeta& meta) = 0;
    virtual void publishHeading(double headingDegTrue,
                                std::optional<double> headingDegMag,
                                const NavValueMeta& meta) = 0;
    virtual void publishDepth(double depthMeters, const NavValueMeta& meta) = 0;
    virtual void publishWind(double windSpeedKnots, double windAngleDeg,
                             const NavValueMeta& meta) = 0;
    virtual void publishVariation(double variationDeg, const NavValueMeta& meta) = 0;
};
```

`NavDataStore` implements this; `Simulator` and `Nmea0183Client` call it. The
store is the only place that knows about the underlying data.

Why an interface and not a direct `NavDataStore*`:

- Sources compile against `INavDataPublisher` only, not the whole store.
- Tests can inject a fake publisher to assert what a source emits.
- The same interface works whether the publisher is built-in (today) or a
  future dynamic plugin.

## Subscriber API

Consumers subscribe to:

- `ownshipChanged()` — fired on any publish **and** on any per-value freshness
  transition (so a value going Stale/Invalid notifies consumers even with no
  new data).

Current state is read via `ownship()`; the position fix's freshness (which
drives the ownship symbol) is available via `positionFreshness()`. Example:

```cpp
connect(store, &NavDataStore::ownshipChanged, this, [this, store] {
    const OwnshipState& o = store->ownship();
    if (o.latitudeDeg.valid() && o.longitudeDeg.valid())
        updateMyOverlay(o.latitudeDeg.value, o.longitudeDeg.value);
});
```

The store mutates its members on the GUI thread; signals are direct by default.

## Freshness lifecycle

A 2 Hz internal `tick()` re-ages **every** value:

```
for each value with a timestamp:
    age = now - value.timestampUtc
    freshness = age<stale ? Fresh : age<invalid ? Stale : Invalid
```

`ownshipChanged()` is emitted only when at least one value transitions (not on
every tick), so idle repaints are avoided. The chart view dims the ownship
triangle and draws a horizontal cancellation slash when the **position** is
`Stale`, and hides the symbol when `Invalid` — the marine convention for an
unreliable / DR fix. The NavData Browser greys each `Stale` row and removes each
`Invalid` row independently.

## Extending: how to add things

### A new publisher (e.g. NMEA 2000)

Same shape as `Simulator` / `Nmea0183Client` — depth and wind from a CAN gateway
simply carry a different `source`:

```cpp
void onPgnDepth(double metres) {
    NavValueMeta m;
    m.source       = QStringLiteral("nmea2000");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    publisher_->publishDepth(metres, m);   // ages independently of position
}
```

The source has no dependency on `NavDataStore`. `MainWindow` wires it up the
same way the simulator and NMEA 0183 client are today.

### A new ownship field

Three small steps:

1. Add the field as a `NavValue` on `OwnshipState`, and include it in
   `NavDataStore::recompute()`'s aging list.
2. If sources will publish it, add a `publishXxx(...)` method to
   `INavDataPublisher` and implement it on `NavDataStore` (call `setValue`, emit
   `ownshipChanged()`).
3. Subscribers that care read `ownship().xxx.value` when `.valid()`; ones that
   don't, ignore it. Additions are non-breaking.

## Extensibility — honest assessment

Strong:

- The publisher/subscriber boundary is real. NMEA 0183/2000, AIS decoders,
  Signal K bridges, and replay sources all slot in as new publishers with no
  store or subscriber changes.
- Per-value source + freshness means mixed-source rigs (position from one bus,
  depth/wind from another) work correctly, and each value ages on its own.
- New fields are non-breaking additions.
- Freshness logic is centralized, not scattered through consumers.

Where it will need work as more nav data lands. These are additions, not
rewrites:

- **Source priority / arbitration.** With multiple sources for the *same* value
  (e.g. two GPS feeds) the behavior today is "whoever called last wins." The
  spec calls for explicit source priority. The hook is `setValue` — it would
  consult a priority/holdoff table before overwriting a value from a
  higher-priority source. No external API change.

- **AIS targets.** Per the spec, AIS needs its own model keyed by MMSI. The
  pattern will mirror ownship: an `AisTargetStore` (a sub-member of the store),
  an `IAisPublisher` interface, and signals like `aisTargetUpdated(mmsi)` /
  `aisTargetExpired(mmsi)`. Independent freshness rules (contacts typically
  expire after a few minutes).

- **Routes & waypoints.** Per the spec, the route/waypoint data model is
  core-owned even though editing and GPX import/export are plugin-provided. Will
  live alongside ownship (`waypoints()`, `routes()`, `activeRoute()`).

- **Per-category thresholds.** Today all values share one stale/invalid pair;
  AIS, instruments, and depth/wind may eventually want their own.

- **Field set vs. typed map.** With ~10 fields the flat `OwnshipState` struct is
  the clearest representation. If the field count grows toward Signal K scope
  (50+ paths) a typed key→value map becomes worth considering. Not needed yet.

None of these breaks the publish/subscribe contract, the interface, or any
existing subscriber.
