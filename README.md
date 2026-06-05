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
are unloaded to bound memory. A small hysteresis margin (load at 1.5×, unload at
2.5× the viewport) keeps panning from thrashing.

Rendering itself is unchanged from the single-folder version: `QGraphicsView`
with one item per feature, cosmetic pens for constant line width, depth-shaded
fills, and sounding labels that appear when zoomed in.

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
src/chart_catalog.*    ChartCatalog — async tree scan, footprints, band, disk cache
src/chart_view.*       QGraphicsView — viewport/zoom selection, async load/unload
src/main_window.*      QMainWindow — toolbar, status bar, folder dialog, QSettings
src/main.cpp           QApplication entry point
```

## Limitations / next steps

- **Coarse cells load whole.** Gap-fill loads each intersecting coarser cell in
  full, even when only a sliver shows in a gap (loading is per-cell, not
  per-region). Coarser cells are generalized so this is usually cheap, but
  viewport clipping / spatial subsetting would reduce it further.
- **No in-memory cache of unloaded cells.** Cells reload (async) when revisited.
  An LRU keyed by path would make back-and-forth panning instant.
- **No load cancellation.** In-flight loads for cells that scrolled away aren't
  cancelled; their results are simply discarded on arrival.
- First scan of a very large tree opens every cell once to read its footprint
  (then caches it). Subsequent launches are instant.
- Symbology is approximate, not S-52, and not for navigation. ENC update files
  (`.001`, …) are not applied.
