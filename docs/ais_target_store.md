# AisTargetStore

The core store for AIS targets (Milestone 5 in `ProjectSpec.md`). It mirrors the
`NavDataStore` model — sources publish through a stable interface, consumers
subscribe to signals, the core owns the data — but is keyed by **MMSI** and
holds many independent vessels instead of one ownship state.

```
+-----------+   publish    +----------------+   signals   +------------------+
| AIS source| -----------> | AisTargetStore | ----------> | overlay, target  |
| (decoder  |              |  QHash<mmsi,   |             | list, CPA calc   |
|  plugin)  |              |    AisTarget>  |             |                  |
+-----------+              +----------------+             +------------------+
```

Today the store is fed by `AisDecoder`, owned by the NMEA 0183 plugin: AIS rides
the same connection as the nav sentences, so the plugin forwards `!AIVDM`/`!AIVDO`
lines to the decoder, which publishes via `IAisPublisher`. Sources reach the
publisher from plugins as `ICoreApi::aisPublisher()`.

## Data model

### `AisTarget`

One tracked vessel, built up from position reports (dynamic) and static/voyage
reports. Optional fields are absent until a report supplies them.

| group | fields |
|-------|--------|
| identity | `mmsi`, `cls` (A/B/Unknown), `name`, `callSign`, `shipType`, `imoNumber` |
| voyage (Class A) | `destination`, `draughtMeters` |
| size | `dimensions` (to bow/stern/port/starboard; `lengthMeters()`, `beamMeters()`) |
| dynamic | `latitudeDeg`, `longitudeDeg`, `cogDegTrue`, `sogKnots`, `headingDegTrue`, `rotDegPerMin`, `navStatus` |
| computed | `cpaMeters`, `tcpaSeconds` (set by a collision component, not from AIS) |
| provenance | `source`, `lastUpdateUtc`, `ageSeconds`, `freshness` |

`AisDimensions` stores the four reference-point offsets AIS transmits; length and
beam are derived. `AisNavStatus` is the standard 0–15 status enum (Class A only).

### `AisFreshness`

Per-target lifecycle, on AIS timescales (minutes, not the ownship's seconds):

| state     | when                                  |
|-----------|---------------------------------------|
| `Current` | `age < staleSeconds` (default 6 min)  |
| `Stale`   | `staleSeconds ≤ age < lostSeconds`    |
| `Lost`    | `age ≥ lostSeconds` (default 12 min)  |

A 1 Hz tick ages every target. On reaching `Lost` the target is **removed** and
`targetExpired(mmsi)` fires; while `Current`/`Stale` it stays in the store (an
overlay would grey a stale contact).

## Publisher API

Sources call `IAisPublisher`. Position and static reports are separate because
AIS sends them in different messages (1/2/3 & 18/19 vs 5 & 24); each carries the
fields it actually has, and either kind keeps the target alive.

```cpp
class IAisPublisher {
    virtual void publishAisPosition(const AisPositionReport& report, const QString& source) = 0;
    virtual void publishAisStatic(const AisStaticData& data, const QString& source) = 0;
};
```

A report only overwrites fields it supplies, so a position update never wipes the
name and a static update never wipes the position. `source` records provenance
(e.g. `"ais.nmea0183"`).

## Subscriber API

```cpp
const QHash<quint32, AisTarget>& targets() const;
const AisTarget* target(quint32 mmsi) const;   // nullptr if not tracked
int count() const;

signals:
    void targetUpdated(quint32 mmsi);   // added, refreshed, or freshness changed
    void targetExpired(quint32 mmsi);   // aged out and removed
```

Consumers connect to the signals and read `targets()`. CPA/TCPA are attached by a
collision component via `setCpaTcpa(mmsi, cpa, tcpa)`, which emits `targetUpdated`.

## AisDecoder

`AisDecoder` turns raw `!AIVDM`/`!AIVDO` lines into store updates. It is
transport-agnostic — fed by whoever owns the connection (today the NMEA 0183
plugin) — and handles:

- **Multi-fragment reassembly** (long messages span several sentences, keyed by
  the sequential message id).
- **6-bit ASCII payload** unpacking with typed bit-field readers.
- The common message types: **1/2/3** (Class A position), **5** (Class A static
  & voyage), **18/19** (Class B position; 19 also carries static), **24** (Class
  B static, Parts A & B). "Not available" sentinels (lat 91°, lon 181°, COG 3600,
  heading 511, SOG 1023, ROT −128) decode to absent fields.

## AisOverlay

`AisOverlay` (`IChartOverlay`) draws each `AisTarget` with `hasPosition()` using
the shared `vessel::drawSymbol` glyph — the same triangle / course-prediction
line / cancellation slash as the ownship symbol, just green so the two are
distinguishable. Targets are dimmed and crossed when their freshness is `Stale`
(default ≥ 6 min since the last message); `Lost` targets are already gone from
the store, so the overlay never has to draw them. The predictor length tracks
the ownship one (a single user-configurable value, kept in sync).

## Not yet (next steps)

- **CPA/TCPA computation** — a component reading ownship (`NavDataStore`) and
  targets, computing closest approach and calling `setCpaTcpa`.
- **Per-source arbitration** — currently last-writer-wins per MMSI (fine for a
  single receiver); priority could be added like the nav store if needed.
