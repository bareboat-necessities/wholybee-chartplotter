# ENC Symbology

How the chartplotter draws S-57 chart features as recognisable nautical symbols
without implementing the full S-52 standard.

S-57 is the IHO data format for Electronic Navigational Charts (ENCs). S-52 is
the companion standard that says *how* to draw each feature — the colours,
boundary patterns, lighted-buoy flares, restricted-area glyphs. S-52 is
expensive to license and complex to implement end-to-end. This app takes a
pragmatic shortcut: it reuses **OpenCPN's GPL `chartsymbols.xml` + atlas PNG**
and implements just enough of the S-52 look-up-table (LUP) selection algorithm
to render correct symbols, line styles, area fills, and rotations.

The result isn't ECDIS-compliant — but visually it matches OpenCPN on the same
charts for the great majority of features.

```
data/                         build tree                          runtime
+--------------------+   gen   +------------+   load   +--------------+
| chartsymbols.xml   | ------> | symbols.bin| -------> |   SymAtlas   |
| (GPL, OpenCPN)     |         | (packed    |          |  LUP engine  |
| 1015 sym defs +    |         |  binary    |          +--------------+
| 2k+ lookups        |         |  ~150 KB)  |                 ^
+--------------------+         +------------+          per-feature query
| rastersymbols-     |    copy                         (objClass, geom,
|   day.png          | --------------------------+     attribute list)
| (sprite atlas)     |                           |
+--------------------+                           v
                                            +---------+
                                            | Painter |
                                            |  blits  |
                                            +---------+
```

## The two source files

Both come from the OpenCPN project (GPL v2) and live under `data/`:

| File | Role |
|------|------|
| `data/chartsymbols.xml` | Vector/raster symbol definitions, lookup tables, colour palettes. Read at build time only. |
| `data/rastersymbols-day.png` | Sprite atlas — every symbol pre-rasterised into a single PNG. Read at runtime, kept resident as a `QPixmap`. |
| `data/rastersymbols-dusk.png`, `data/rastersymbols-dark.png` | Same atlas at dusk/night colour temperatures. Bundled but not yet switched at runtime. |

OpenCPN distributes both so the symbology engine can either rasterise vectors
or blit pre-baked tiles. We use the **pre-baked tiles**: faster at runtime
(one `drawPixmap` per symbol, no path construction) and the colours are baked
in correctly.

## Build-time tool: `gen_symbols`

`tools/gen_symbols.cpp` parses the XML and emits a compact binary lookup table
(`symbols.bin`). CMake builds it as a host-side executable and runs it once per
build, with the Qt bin dir prepended to `PATH` so `Qt6Core.dll` is loadable.

The tool's output is reproducible from the same XML — there's no state in the
build dir beyond `symbols.bin`. A typical run prints:

```
gen_symbols: 1015 syms, 1806 lookups (7 fallbacks), 2470 conds, 84 attrs,
             53 lines, 12 fills
gen_symbols: wrote symbols.bin (152872 bytes)
```

`fallbacks` are entries the tool synthesises to fill in CS-procedure gaps
(see [Fallbacks](#fallbacks-when-the-xml-doesnt-resolve)).

## Binary format: `symbols.bin`

A single packed binary, little-endian, magic `"SYM\x05"`. Layout:

```
Header (28 bytes)
  magic[4]   "SYM\x05"
  symCount   number of SymRecord
  lupCount   number of LupRecord
  condCount  number of CondRecord (condition pool)
  attrCount  number of AttrRecord (relevant-attribute acronyms)
  lineCount  number of LineStyleRecord (LS pool)
  fillCount  number of FillStyleRecord (AC pool)

SymRecord   x symCount    (36 bytes each) -- atlas tiles
LupRecord   x lupCount    (20 bytes each) -- one lookup
CondRecord  x condCount   (32 bytes each) -- pooled conditions
AttrRecord  x attrCount   (8  bytes each) -- attribute acronyms
LineStyleR  x lineCount   (8  bytes each) -- dedup'd LS() styles
FillStyleR  x fillCount   (4  bytes each) -- dedup'd AC() styles
```

Key per-record shapes:

```
SymRecord       name[24], atlas_x, atlas_y, width, height, pivot_x, pivot_y
LupRecord       objClass[8], geomType, dispCat, nConds, rotMode,
                condStart, symIdx, lineStyleIdx, fillStyleIdx
CondRecord      attr[8], value[24]
LineStyleR      pattern, width, r, g, b, _pad[3]
FillStyleR      r, g, b, a
```

`LupRecord`s are grouped: every lookup for a `(objClass, geomType)` is
contiguous, which lets the runtime build a single hash from class+geom to a
`[first, count)` range with no per-lookup index entry.

Conditions and line/fill styles are pooled and dedup'd. Many lookups share the
same `(LS, SOLD, 1, CHGRD)` outline; the pool collapses them, so a typical
build has tens of style records — not hundreds.

`0xFFFF` is the sentinel for "absent" in any `*Idx` field
(`symIdx`/`lineStyleIdx`/`fillStyleIdx`).

## Runtime: `SymAtlas` + matcher

`src/sym_atlas.{hpp,cpp}` owns everything related to symbols at runtime.

**Lifecycle:**

1. `ChartView` constructs and calls `symAtlas_.load("symbols.bin",
   "rastersymbols-day.png")` — synchronous, on the GUI thread, exactly once.
2. The PNG goes into a `QPixmap` (GPU-backed on the Qt raster/GL backends).
3. The binary is read into immutable vectors and a `QHash`.
4. After load the data is **never mutated** — every read is safe to call from
   any worker thread.
5. `ChartView` hands the relevant-attribute set to `chart::setSymbologyAttrs`,
   so the loader knows exactly which S-57 attributes it must read into each
   feature.

**Thread model:**

```
GUI thread                          worker thread (cell load)
+---------------+                   +-------------------------+
| SymAtlas      |                   | chart_loader            |
|  load() once  |   <-- queries --  |  reads f.attrs from GDAL|
|  immutable    |                   |  symbolForFeature(...)  |
+---------------+                   |  → SymHit               |
        ^                           +-------------------------+
        | drawPixmap (paintEvent)            |
        |                                    v
+---------------+                   +-------------------------+
| QPainter blit |                   | BuiltCell (built path,  |
+---------------+                   |  symbols, fills, lines) |
                                    +-------------------------+
```

The matcher (`symbolForFeature`) takes:

```cpp
SymHit symbolForFeature(const QByteArray& objClass,
                        SymGeom geom,              // Point, Line, Area
                        const AttrList& attrs);    // (acronym, value) pairs
```

and returns:

```cpp
struct SymHit {
    uint16_t      symIdx;        // atlas tile, or kNoSymbol
    float         rotationDeg;   // CW from true north
    bool          hasLine;
    SymLineStyle  line;          // pattern, width, RGB
    bool          hasFill;
    SymFillStyle  fill;          // RGBA (alpha from S-52 transparency)
};
```

A single hit can carry **any combination** of symbol, line, and fill — a
TSS-zone area (`TSEZNE`) gets just a fill, a TSS-boundary line (`TSSBND`) gets
just a dashed line, an anchorage (`ACHARE`) gets a centred symbol plus a
dashed boundary.

### The best-match selection algorithm

For each `(objClass, geomType)`, the binary holds one or more `LupRecord`s.
Each lookup has zero or more attribute conditions. The runtime picks one:

1. Walk every lookup for `(objClass, geomType)`.
2. A lookup **matches** when every one of its conditions is satisfied by the
   feature's attributes.
3. Among matched lookups, the **most-specific one wins** (highest `nConds`).
4. If no conditional lookup matches, the class's no-condition default wins.
5. If there's no default either, return `kNoSymbol` (renders as a dot).

A condition can be:

| Form | Encoding | Meaning |
|------|----------|---------|
| `BOYSHP4` | `attr="BOYSHP", value="4"` | The attribute equals this value. |
| `COLOUR3,4,3` | `attr="COLOUR", value="3,4,3"` | The attribute equals this comma-joined multi-value. |
| `ORIENT ` (acronym alone) | `attr="ORIENT", value="*"` | The attribute is *present* on the feature, any value. |

The `"*"` presence sentinel is critical. The XML uses it for lookups like
`<attrib-code>ORIENT </attrib-code>` (note the trailing space — acronym only,
no value). Without it, every TSS-lane arrow was filtered out because nothing
"equals empty string"; with it, those lookups match exactly when the feature
has an ORIENT attribute.

## S-52 instruction support

Each lookup's `<instruction>` is an S-52 mini-language: a semicolon-separated
list of drawing primitives. We parse the first occurrence of each kind into
the binary.

| Instruction | Meaning | Support |
|-------------|---------|---------|
| `SY(NAME)` | Stamp a symbol from the atlas at the feature point/centroid. | ✅ |
| `SY(NAME, ORIENT)` | …rotated by the feature's ORIENT attribute. | ✅ |
| `SY(NAME, OBJNAM)` | …with text-label rotation. | Falls back to no rotation. |
| `SY(NAME, <numeric>)` | …fixed rotation. | Falls back to no rotation. |
| `LS(pattern, width, colour-token)` | Solid/dashed/dotted line. | ✅ |
| `LC(symbol-name)` | "Line complex" — stroke a symbol along the path. | Fallback only (dashed magenta). |
| `AC(colour-token[, τ])` | Area-colour wash, optional S-52 transparency. | ✅ |
| `AP(pattern-name)` | Area-pattern fill (hatching, stipple). | ❌ |
| `CS(procedure)` | Conditional symbology procedure. | ❌ — synthetic fallbacks instead. |
| `TX/TE(...)` | Text labels. | ❌ |

### Rotation (`SY(..., ORIENT)`)

A lookup's `rotMode` is set to `1` when its `SY()` second argument is
`ORIENT`. At match time, the runtime reads the feature's `ORIENT` attribute
(degrees CW from true north) and stores it in `SymHit::rotationDeg`. The
painter draws via:

```cpp
t.translate(d.x(), d.y());
t.rotate(rotationDeg);
t.translate(-pivot.x(), -pivot.y());
p.drawPixmap(QPointF(0,0), atlas, src);
```

Our scene is north-up Mercator with Y flipped, so `QPainter::rotate(orient)`
maps an "ORIENT 240°" feature to a symbol whose own "up" points at compass
bearing 240° — directly matching S-57 conventions. No coordinate juggling.

156 lookups across the XML use this — TSS lane arrows, deep-water route
arrows, current arrows, traffic-direction markers, fairway centerlines.

### Line styles (`LS()`)

`LS(pattern, width, colour-token)` becomes a `LineStyleRecord`. `colour-token`
is resolved against the **DAY_BRIGHT** colour table at bake time, baking the
RGB directly into the record:

| Pattern | Maps to |
|---------|---------|
| `SOLD` | `Qt::SolidLine` |
| `DASH` | `Qt::DashLine` |
| `DOTT` | `Qt::DotLine` |

For a **Line** feature, the line style colours the geometry itself
(`TSSBND` — dashed magenta traffic boundary). For an **Area** feature, the
line style colours the boundary outline (`ACHARE` — dashed magenta anchorage
edge).

### Line-complex fallback (`LC()`)

`LC(symbol-name)` would stroke an atlas symbol along the line — patterns like
the squiggle on submarine cables. We don't implement this. Instead, when a
lookup carries `LC()` but no `LS()`, `firstLS` synthesises a fallback
`LS(DASH, 2, CHMGF)` — the universal "light magenta" colour S-52 uses for
soft warning boundaries (restricted areas, deep-water route boundaries,
TSS zones).

This is visually close at typical zoom levels and lights up about 80 boundary
styles that would otherwise render as grey.

### Area fills (`AC()`)

`AC(colour-token[, τ])` becomes a `FillStyleRecord`. The S-52 transparency
factor (τ in 0..4) is resolved to an 8-bit alpha:

```
alpha = 255 * (1 - τ / 4)

τ = 0  →  α = 255  (opaque)
τ = 1  →  α = 191
τ = 2  →  α = 127
τ = 3  →  α =  63  (the common "soft tint")
τ = 4  →  α =   0  (fully transparent)
```

When a `BuiltCell` is constructed and the matched lookup has a fill, the
`OtherArea` branch switches from polyline clipping (open outlines) to ring
clipping (closed polygons) and gives the `BuiltPath` both a fill brush and an
outline pen. The rendered effect is a translucent colour wash — TSEZNE
traffic-separation zones, RAPIDS, RUNWAY surfaces, etc.

### Conditional symbology procedures (`CS()`) — not implemented

Procedures like `CS(LIGHTS05)`, `CS(SOUNDG02)`, `CS(OBSTRN04)` run code at
render time to pick a symbol based on attribute combinations. Implementing
them correctly is a real undertaking — they branch on multiple attributes,
nested geometric tests, and chart-context state.

Instead, we synthesise **conditional fallbacks** for the classes that would
otherwise render as nothing. See the next section.

## Fallbacks: when the XML doesn't resolve

The XML's lookups for some classes are CS-only — they call `CS(LIGHTS05)`
with no direct `SY()`. Since we don't execute CS procedures, the runtime
would resolve to `kNoSymbol` and render those features as a magenta dot.

`gen_symbols` patches this by inserting synthetic lookups for a small list of
classes, in `kFallbacks[]`. Each entry is inserted **only if** the class
already has no no-condition default in `kept` — so any genuine XML default
always wins.

Current fallbacks:

| Class | Condition | Symbol | Why |
|-------|-----------|--------|-----|
| `LIGHTS` | `COLOUR=3` | `LIGHTS11` (red) | Replicates the most common branch of `CS(LIGHTS05)`. |
| `LIGHTS` | `COLOUR=4` | `LIGHTS12` (green) | |
| `LIGHTS` | `COLOUR=1` | `LIGHTS13` (white) | |
| `LIGHTS` | `COLOUR=6` | `LIGHTS13` (yellow) | |
| `LIGHTS` | *(default)* | `LIGHTS14` (magenta) | Multi-colour lights and missing-colour lights. |
| `UWTROC` | *(default)* | `UWTROC03` | Underwater rock that covers/uncovers. |
| `RESARE` | *(default)* | `ENTRES61` + dashed magenta boundary | RESARE features without `CATREA` (entry-restricted Regulated Navigation Areas). |

The `LIGHTS` color variants use the same best-match algorithm as XML
lookups: the conditional ones win when `COLOUR` is present and matches; the
default catches everything else.

## Attributes: what the loader reads

The chart loader reads a **fixed set** of S-57 attributes from each feature —
exactly the union of those referenced by any condition in any lookup. The
set is baked into `symbols.bin` and exposed via `SymAtlas::relevantAttrs()`,
which `ChartView` hands to `chart::setSymbologyAttrs()` at startup:

```cpp
if (symAtlas_.isLoaded())
    chart::setSymbologyAttrs(symAtlas_.relevantAttrs());
```

A typical build pulls in ~80 attributes: `BOYSHP`, `CATLAM`, `COLOUR`,
`COLPAT`, `CATCAR`, `CATSPM`, `WATLEV`, `CATWRK`, `ORIENT`, `TRAFIC`,
`RESTRN`, etc. The loader resolves each layer's field indices **once per
layer** and reads only those fields per feature.

### Field-type normalisation

A subtle GDAL pitfall: the S-57 driver returns multi-valued S-57 attributes
(`COLOUR`, `COLPAT`, `NATSUR`, …) as `OFTStringList`. Calling
`OGR_F_GetFieldAsString` on a StringList returns a *formatted* string like
`"(1:4)"` — not the bare value `"4"` that the lookup condition expects.

`normalizedFieldValue()` in `chart_loader.cpp` handles every field type
explicitly:

```
OFTInteger          → "4"
OFTInteger64        → "4"
OFTReal             → "4"          (cast to long long)
OFTIntegerList      → "4" or "1,4" (joined)
OFTRealList         → "4" or "1,4"
OFTStringList       → "4" or "1,4" (walked element-by-element)
default / OFTString → strip spaces from OGR_F_GetFieldAsString
```

Without the StringList case, **every COLOUR-conditioned lookup silently
fails** — green buoys default to the uncoloured BOYGEN03 outline, green
lights to magenta. The visual symptom is "buoys look unfilled and lights are
all wrong colour"; the root cause is GDAL handing back a list-formatted
string the matcher can't parse.

## Table selection

S-52 ships its symbology in multiple tables per geometry type:

| Geometry | Tables | Preferred |
|----------|--------|-----------|
| Point | `Paper`, `Simplified` | `Paper` (realistic buoy/beacon shapes) |
| Line | `Lines` | `Lines` |
| Area | `Plain`, `Symbolized` | `Symbolized` (modern S-52 default) |

`preferredTable()` in `gen_symbols.cpp` picks the preferred one when present
and falls back to the alternate if not. Only lookups in the chosen table for
each class are kept; the others are dropped from the binary.

## Colour resolution

The `<color-tables>` section of `chartsymbols.xml` defines named colour
tokens (`CHGRD`, `TRFCD`, `LANDA`, `CHMGF`, …) in four palettes:

| Table | When |
|-------|------|
| `DAY_BRIGHT` | Daylight, normal contrast |
| `DAY_BLACKBACK` | Daylight, inverted background |
| `DAY_WHITEBACK` | Daylight, white background |
| `DUSK`, `NIGHT` | Reduced brightness for night vision |

`gen_symbols` parses the **`DAY_BRIGHT`** table only. The token-to-RGB map is
held in memory during the build and used to resolve every colour reference
in `LS()` and `AC()` instructions. The RGB is baked into the binary —
runtime knows nothing about the tokens.

Switching to dusk/night would mean either baking the alternate palettes into
the binary as parallel tables, or keeping the token names in the records and
resolving at draw time. Neither is wired up today.

## The rendering pipeline

How a feature becomes pixels:

```
1. chart_loader reads the ENC cell:
     - feature kind from layer name (DEPARE → DepthArea, LIGHTS → Point, ...)
     - objClass copied for symbol-bearing kinds (Point, OtherArea, OtherLine)
     - relevant S-57 attributes read into f.attrs

2. chart_view::buildCell() runs on a worker thread:
     - For each feature, query atlas->symbolForFeature(objClass, geom, attrs)
     - Apply the SymHit:
         hit.symIdx        → BuiltSymbol at point or centroid
         hit.rotationDeg   → on BuiltSymbol
         hit.line          → BuiltPath pen colour/width/style
         hit.fill          → BuiltPath brush colour (and switch to ring clip)
     - Sort BuiltPaths by z (areas under, lines over, contour-aware)

3. chart_view::paintEvent() on the GUI thread:
     - Walk BuiltPath, set pen+brush, drawPath
     - Walk BuiltSymbol, atlas.draw(p, idx, screenPoint, rotation)
         (one drawPixmap with the source rect → cheap GPU blit)
```

## Source layout

```
data/
  chartsymbols.xml          GPL OpenCPN source — build time only
  rastersymbols-day.png     atlas — runtime
  rastersymbols-dusk.png
  rastersymbols-dark.png

tools/
  gen_symbols.cpp           build-time tool: XML → symbols.bin

src/
  sym_atlas.hpp/.cpp        runtime atlas + LUP matcher
  chart_loader.hpp/.cpp     S-57 reader; normalizedFieldValue()
  chart_view.hpp/.cpp       buildCell() applies SymHit to BuiltPath/Symbol

CMakeLists.txt              gen_symbols target, custom command,
                            data-file copy POST_BUILD
```

## Known limitations

| Gap | Effect | Workaround |
|-----|--------|------------|
| `CS()` procedures unhandled | Lights don't show sector arcs; soundings use our text not S-52 split-depth glyph; some restricted-area variants pick the wrong symbol. | Synthetic fallbacks for `LIGHTS` / `RESARE` / `UWTROC` cover the high-traffic cases. |
| `LC()` line-complex unhandled | Cables and a few other patterned lines render as a generic dashed magenta. | Universal LC fallback in `firstLS`. |
| `AP()` area patterns unhandled | Restricted-area interiors don't show their hatching/stipple. | Boundary + centroid symbol still drawn. |
| `TX/TE()` text labels unhandled | Buoy names, light characteristics, etc. don't appear. | — |
| Day-mode palette only | Atlas PNGs for dusk/night are bundled but the lookup colours are baked from `DAY_BRIGHT`. | — |
| Multi-colour S-57 attribute conditions are exact-string | A buoy with `COLOUR=1,4` matches a `COLOUR=1,4` rule but not a `COLOUR=1` rule. | Matches the XML's encoding; the same way OpenCPN's tables work. |

None of these are blockers for general navigation viewing — the app reads
real ENC charts and renders the great majority of features
recognisably — but they're the gaps to close if you want pixel-parity with
OpenCPN.
