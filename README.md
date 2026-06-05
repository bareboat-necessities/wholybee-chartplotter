# Marine Chart Viewer (Qt 6)

A small ENC (S-57) chart viewer built with **Qt 6** and **GDAL**. This is the
Qt port of the GTK 4 version: same behaviour (pick a folder of ENC cells, then
pan and zoom around one stitched chart), rebuilt on `QGraphicsView`.

## What changed from the GTK 4 version

The chart parsing and projection are **identical** — `chart_loader.*` and
`projection.hpp` are byte-for-byte the same files, because they contain no UI
code (just GDAL + math). Only the view and the application shell were rewritten:

| Concern            | GTK 4 build                        | Qt 6 build                              |
|--------------------|------------------------------------|-----------------------------------------|
| Chart parsing      | `chart_loader.*` (GDAL)            | **same file, unchanged**                |
| Projection         | `projection.hpp`                   | **same file, unchanged**                |
| Canvas             | `GtkDrawingArea` + Cairo, manual transform & culling | `QGraphicsView`/`QGraphicsScene`, one item per feature |
| Pan                | hand-written drag math             | `setDragMode(ScrollHandDrag)`           |
| Zoom-under-cursor  | hand-written focus math            | `AnchorUnderMouse` + `scale()`          |
| Fit                | manual scale-to-bounds             | `fitInView()`                           |
| Culling            | per-feature bbox test each frame   | scene BSP index (free)                  |
| Constant line width| pixels because coords were manual  | `QPen::setCosmetic(true)`               |
| Settings           | GKeyFile                           | `QSettings`                             |

The view shrank: `QGraphicsView` supplies the transform, spatial indexing, and
culling, so the hand-rolled viewport code is gone. Each ENC feature becomes a
`QGraphicsPathItem` (areas/lines), a `QGraphicsSimpleTextItem` (soundings), or a
`QGraphicsEllipseItem` (point objects). Soundings and point symbols use
`ItemIgnoresTransformations` so they stay a fixed pixel size, and are hidden
when zoomed out past ~20 km across.

Note on coordinates: Qt scene Y grows **downward**, so geometry is stored with
Y negated to keep north up; the cursor read-out negates it back before the
inverse-Mercator conversion.

## Building

Requires Qt 6 (Widgets), GDAL with the S-57 driver, CMake ≥ 3.16, C++17.

### Windows (MSYS2 — recommended)

Open the **MINGW64** shell:

```bash
pacman -S --needed \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-qt6-base \
    mingw-w64-x86_64-gdal

cmake -S . -B build -G Ninja
cmake --build build
./build/chartviewer.exe
```

Alternatively use the official Qt online installer; point CMake at it with
`-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64` (or the MSVC kit).

To ship the `.exe`, run `windeployqt build/chartviewer.exe` to gather the Qt
DLLs and plugins, and copy GDAL's DLLs plus its data directory (set
`GDAL_DATA`).

### Linux

```bash
# Debian/Ubuntu
sudo apt install cmake g++ qt6-base-dev libgdal-dev
# Fedora
# sudo dnf install cmake gcc-c++ qt6-qtbase-devel gdal-devel

cmake -S . -B build
cmake --build build
./build/chartviewer
```

### macOS (Homebrew)

```bash
brew install cmake qt gdal
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/chartviewer.app/Contents/MacOS/chartviewer
```

> **GDAL discovery.** The CMake script tries `find_package(GDAL)` first
> (uses `GDAL::GDAL`) and falls back to pkg-config (`PkgConfig::GDAL`). Both
> paths are covered, so no edits should be needed.

## Test data

Free ENC cells for US waters come from NOAA — search "NOAA ENC direct
download". Unzip an exchange set and point the app at the top folder; it finds
the `.000` cells underneath (typically in `ENC_ROOT/<cell>/`).

## Controls

- **Drag** — pan (hand cursor).
- **Scroll wheel** — zoom, centred on the cursor.
- **Fit** — re-frame the whole dataset.
- Cursor latitude/longitude shows at the right of the status bar.

## Layout

```
src/projection.hpp     Mercator <-> lon/lat helpers          (shared, unchanged)
src/chart_loader.*     ChartSet: GDAL S-57 reading + model    (shared, unchanged)
src/chart_view.*       QGraphicsView canvas: scene, pan/zoom, LOD
src/main_window.*      QMainWindow: toolbar, status bar, folder dialog, QSettings
src/main.cpp           QApplication entry point
```

## Known limitations / next steps

- **Loading is synchronous** and briefly blocks the UI on large sets. In Qt this
  is a small change: run `ChartSet::loadDirectory` via `QtConcurrent::run` and
  handle completion with a `QFutureWatcher` on the GUI thread (sketched in a
  comment in `main_window.cpp`).
- **One item per feature** is fine for thousands–tens-of-thousands of features.
  For very dense multi-cell sets, batch static geometry into fewer items, add a
  quad-/R-tree, and/or simplify lines at low zoom.
- Symbology is an approximation, **not** S-52, and not for navigation.
- ENC update files (`.001`, `.002`, …) are not applied; only base cells read.
