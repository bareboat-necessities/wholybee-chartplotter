# Marine Chart Viewer (Qt 6)

A streamlined ENC (S-57) chart viewer built with **Qt 6** and **GDAL**. Point it
at the root of a directory tree containing hundreds of ENC cells; it catalogs
them, then loads only the cells visible in the current view, on background
threads, as you pan and zoom.

## How it works

### 1. Catalog the tree (cheap, cached)

When you open a folder, a background scan walks the tree for ENC base cells
(`*.000`) and records, for each cell:

- its **footprint** (bounding box), read cheaply from the small `M_COVR`
  coverage layer rather than the full geometry, and
- its **usage band** (navigational purpose: 1 = overview … 6 = berthing), which
  comes for free from the cell name — the band digit is the 3rd character of an
  ENC filename (e.g. `US`**`5`**`FL14M.000` is a harbour cell).

Footprints are cached to disk (keyed by file path + size + modified-time, one
cache file per root), so subsequent launches skip re-reading the cells.

### 2. Select by viewport + zoom (with gap-fill quilting)

On every pan/zoom (debounced), the view:

- computes the visible world rectangle and the zoom-appropriate *target* band,
- selects every available band from overview up to that target (`1..maxBand`)
  whose footprint intersects the viewport, and
- draws them **band-major**: coarser bands underneath, finer bands on top. A
  finer cell's opaque area fills occlude the coarser cell within its footprint,
  while anywhere the finer band has no coverage, the next coarser available band
  shows through. That is the gap fill — missing bands are simply skipped, so a
  gap is filled by whatever the next *available* coarser band is.

If an area has no coverage at or below the target band (only finer data exists),
it falls back to the coarsest band finer than the target so the screen isn't
blank.

### 3. Load asynchronously

Newly-visible cells are loaded on a `QThreadPool` — each worker opens its own
GDAL handle (the thread-safe usage pattern) and returns parsed geometry, which
the UI thread turns into scene items. Cells that scroll well outside the view
are unloaded to bound memory (their parse stays in the cache; see below). A
hysteresis margin — load just beyond the viewport edge, unload only well past it
— keeps panning from thrashing; the exact margins are described in §4.

Rendering itself is unchanged from the single-folder version: `QGraphicsView`
with one item per feature, cosmetic pens for constant line width, depth-shaded
fills, and sounding labels that appear when zoomed in.

### 4. Cache parsed cells (LRU) and clip per region

Two layers keep panning and zooming fast:

**In-memory LRU cache** (`FeatureCache`). Parsing a cell — GDAL open, S-57 layer
walk, projection — is the expensive step, and its output is just plain vectors
of points. We keep that parsed result in an LRU keyed by file path, so revisiting
a cell that scrolled off rebuilds its scene items straight from memory with no
disk or GDAL round-trip. The cache is bounded by a soft byte budget and entry
count (256 MB / 256 cells by default); when a load pushes it over, the
least-recently-used cells are evicted. Cells currently on screen are *pinned* and
never evicted, so a tight budget can never drop visible geometry — it just holds
the live set and trims everything else.

**Per-region clipping** (`geom_clip`). The cached value is the cell's *full*
parse, independent of any viewport. Scene items are built by clipping that parse
to a region a little larger than the view (Sutherland–Hodgman for area rings,
Cohen–Sutherland for contour/coastline polylines, a rect test for soundings and
points). This matters most for gap-fill: a coarse cell that contributes only a
sliver in a gap would otherwise drag a basin-spanning polygon into the scene, and
Qt would traverse and rasterize all of it every frame. Clipped, it carries only
roughly screen-sized geometry, so per-frame cost stays low. Caching the full
parse (rather than clipped output) means a later pan re-clips the same cached
cell to a new region for free — no reload.

The clip region is the same box used for unload hysteresis, and it is always
larger than the visible viewport, so the straight edges clipping introduces fall
off-screen. Concretely, the load/re-clip trigger sits half a viewport-width
beyond each edge while the clip (and unload) box sits one and a half
viewport-widths beyond — a full viewport-width of margin. A cell is re-clipped
only once the view has moved far enough that its stored clip box no longer covers
that inner trigger box, which is still a full viewport-width short of the old clip
edge. So the visible area never reaches a clip boundary and **no blank slivers
ever appear**.

## Controls

- **Drag** — pan. **Scroll wheel** — zoom (centred on cursor).
- **Fit** — frame the whole catalog (all cells, not just loaded ones).
- Status bar: root folder + scan summary (left), band / cells shown (middle),
  cursor lat/long (right).

## Building

Requires Qt 6 (Widgets **and Concurrent**, both part of Qt Base), GDAL with the
S-57 driver, CMake ≥ 3.16, C++17. See `BUILDING_WINDOWS.md` for the
Visual Studio (MSVC) path; quick Linux build:

```bash
sudo apt install cmake g++ qt6-base-dev libgdal-dev
cmake -S . -B build && cmake --build build
./build/chartviewer
```

On Windows, remember `GDAL_DATA` must point at GDAL's data folder so the S-57
driver finds `s57objectclasses.csv` / `s57attributes.csv`.

## Test data

Free ENC cells for US waters come from NOAA ("NOAA ENC direct download"). Unzip
the exchange set(s) anywhere under one root and point the app at that root.

## Layout

```
src/projection.hpp     Mercator <-> lon/lat helpers
src/chart_loader.*     chart:: free functions — per-cell load + cheap extent (GDAL)
src/geom_clip.hpp      pure polygon/polyline/point clipping math (no Qt/GDAL)
src/feature_cache.hpp  FeatureCache — LRU of parsed cells, keyed by path
src/chart_catalog.*    ChartCatalog — async tree scan, footprints, band, disk cache
src/chart_view.*       QGraphicsView — viewport/zoom selection, async load/unload
src/main_window.*      QMainWindow — toolbar, status bar, folder dialog, QSettings
src/main.cpp           QApplication entry point
```

`geom_clip.hpp` and `feature_cache.hpp` are deliberately free of Qt and GDAL so
they can be unit-tested in isolation — see `test_clip.cpp`.

## Limitations / next steps

- **No load cancellation.** In-flight loads for cells that scrolled away aren't
  cancelled; their results are simply discarded on arrival (but still cached, so
  the work isn't wasted if the cell is revisited).
- First scan of a very large tree opens every cell once to read its footprint
  (then caches it). Subsequent launches are instant.
- Symbology is approximate, not S-52, and not for navigation. ENC update files
  (`.001`, …) are not applied.
