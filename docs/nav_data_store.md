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
| NMEA,    |                  | OwnshipState |                 | view,     |
| Signal K |                  | freshness    |                 | status,   |
| ...      |                  |              |                 | future    |
|          |                  |              |                 | instr.    |
+----------+                  +--------------+                 +-----------+
```

## Data model

### `NavValueMeta`

Per-value provenance:

| field          | meaning                                                |
|----------------|--------------------------------------------------------|
| `source`       | e.g. `"simulator"`, `"nmea0183.serial1"`, `"signalk"`  |
| `timestampUtc` | when the source produced this value (UTC)              |
| `ageSeconds`   | cached; updated by the store's freshness tick          |
| `valid`        | whether the receiver still considers the value usable  |

### `OwnshipState`

Live ownship navigation as typed C++ with optional fields. Names mirror
Signal K paths conceptually (`navigation.position`,
`navigation.courseOverGroundTrue`, ...) but storage is a flat struct, not a
JSON tree.

```cpp
std::optional<double> latitudeDeg;
std::optional<double> longitudeDeg;
std::optional<double> cogDegTrue;
std::optional<double> sogKnots;
std::optional<double> headingDegTrue;
std::optional<double> headingDegMag;
std::optional<double> variationDeg;
std::optional<double> depthMeters;
std::optional<double> windSpeedKnots;
std::optional<double> windAngleDeg;
NavValueMeta meta;
```

An optional is absent when no source has supplied a value for that field.

### `NavFreshness`

State machine reflecting how stale the ownship fix is:

| state     | when                                  |
|-----------|---------------------------------------|
| `Fresh`   | `age < staleSeconds`                  |
| `Stale`   | `staleSeconds ≤ age < invalidSeconds` |
| `Invalid` | `age ≥ invalidSeconds` (or no fix ever) |

Thresholds are runtime-configurable via `setStaleSeconds()` /
`setInvalidSeconds()`; defaults are 5 s and 30 s.

## Publisher API

Sources call into the store through `INavDataPublisher`:

```cpp
class INavDataPublisher {
    virtual void publishOwnshipPosition(double latDeg, double lonDeg,
                                        const NavValueMeta& meta) = 0;
    virtual void publishCogSog(double cogDegTrue, double sogKnots,
                               const NavValueMeta& meta) = 0;
    virtual void publishHeading(double headingDegTrue,
                                std::optional<double> headingDegMag,
                                const NavValueMeta& meta) = 0;
};
```

`NavDataStore` implements this interface; `Simulator` calls it. The store is the
only place that knows about the underlying data.

Why an interface and not a direct `NavDataStore*`:

- Sources compile against `INavDataPublisher` only, not the whole store.
- Tests can inject a fake publisher to assert what a source emits.
- The same interface works whether the publisher is built-in (today) or a
  future dynamic plugin.

## Subscriber API

Consumers subscribe to:

- `ownshipChanged()` — any ownship field updated.
- `freshnessChanged(NavFreshness)` — fix transitioned between states.

Current state is read via `ownship()` / `freshness()`. Example:

```cpp
connect(store, &NavDataStore::ownshipChanged, this, [this, store] {
    const OwnshipState& o = store->ownship();
    if (o.latitudeDeg && o.longitudeDeg) updateMyOverlay(*o.latitudeDeg, *o.longitudeDeg);
});
```

The store mutates its members on the GUI thread; signals are direct by default.

## Freshness lifecycle

A 2 Hz internal `tick()` recomputes the age of the position fix:

```
age = now - meta.timestampUtc
freshness = age<stale ? Fresh : age<invalid ? Stale : Invalid
meta.valid = (freshness != Invalid)
```

`freshnessChanged` is emitted only on transitions. The chart view dims the
ownship triangle and draws a horizontal cancellation slash through the position
when `Stale`, and hides the symbol entirely when `Invalid` — the marine
convention for an unreliable / DR fix.

## Extending: how to add things

### A new publisher (e.g. NMEA0183)

Same shape as `Simulator`:

```cpp
class Nmea0183Source : public QObject {
    Q_OBJECT
public:
    Nmea0183Source(INavDataPublisher* pub, QIODevice* dev, QObject* p = nullptr);
private:
    INavDataPublisher* publisher_;
    void onRmc(double lat, double lon, double cog, double sog, QDateTime utc) {
        NavValueMeta m;
        m.source       = QStringLiteral("nmea0183.serial1");
        m.timestampUtc = utc;
        publisher_->publishOwnshipPosition(lat, lon, m);
        publisher_->publishCogSog(cog, sog, m);
    }
};
```

The source has no dependency on `NavDataStore`. `MainWindow` wires it up the
same way the simulator is today.

### A new ownship field

Three small steps:

1. Add the field as `std::optional<T>` on `OwnshipState`.
2. If sources will publish it, add a `publishXxx(...)` method to
   `INavDataPublisher` and implement it on `NavDataStore` (set field, emit
   `ownshipChanged()`).
3. Subscribers that care read `ownship().xxx`; ones that don't, ignore it.
   Additions are non-breaking.

## Extensibility — honest assessment

Strong:

- The publisher/subscriber boundary is real. NMEA, AIS decoders, Signal K
  bridges, and replay sources all slot in as new publishers with no store or
  subscriber changes.
- Optional fields on `OwnshipState` mean field additions are non-breaking.
- Freshness logic is centralized, not scattered through consumers.

Where it will need work as more nav data lands. These are additions, not
rewrites:

- **Per-value meta.** `OwnshipState::meta` only reflects the position fix
  today; the meta arguments passed to `publishCogSog`/`publishHeading` are
  currently accepted but **not stored**. When the spec's "individual fields
  age independently" matters (heading flatlines while position is still
  fresh), `OwnshipState` will grow per-field meta. The publisher API stays
  the same.

- **Source priority / arbitration.** With multiple position sources
  (NMEA + Signal K + sim) the behavior today is "whoever called last wins."
  The spec calls for explicit source priority. The hook is
  `NavDataStore::publishOwnshipPosition` — it would consult a priority table
  before accepting the update. No external API change.

- **AIS targets.** Per the spec, AIS needs its own model keyed by MMSI. The
  pattern will mirror ownship: an `AisTargetStore` (a sub-member of the
  store), an `IAisPublisher` interface, and signals like
  `aisTargetUpdated(mmsi)` / `aisTargetExpired(mmsi)`. Independent freshness
  rules (contacts typically expire after a few minutes).

- **Routes & waypoints.** Per the spec, the route/waypoint data model is
  core-owned even though editing and GPX import/export are plugin-provided.
  Will live alongside ownship (`waypoints()`, `routes()`, `activeRoute()`).

- **Stale rules per category.** Today only ownship has a freshness state;
  AIS, instruments, and depth/wind will want their own thresholds.

- **Field set vs. typed map.** With ~10 fields the flat `OwnshipState` struct
  is the clearest representation. If the field count grows toward Signal K
  scope (50+ paths) a typed key→value map becomes worth considering. Not
  needed yet.

None of these breaks the publish/subscribe contract, the interface, or any
existing subscriber. The core data model can grow without invalidating
sources or consumers — which is exactly what an extensible foundation
should mean.
