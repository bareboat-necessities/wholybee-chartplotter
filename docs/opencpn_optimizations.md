# OpenCPN Rendering Optimizations vs. Ours

A comparison of the chart-rendering optimizations used by
[OpenCPN](https://github.com/OpenCPN/OpenCPN) against what this app does today,
written after inspecting OpenCPN's `master` sources (June 2026; files under
`gui/src/` — `chartdbs.cpp`, `chartdb.cpp`, `senc_manager.cpp`, `o_senc.cpp`,
`s57chart.cpp`, `quilt.cpp`, `gl_tex_cache.cpp`). The motivating question: when
ENC charts are installed, OpenCPN walks the user through **"Rebuild chart
database"** and **"Prepare all ENC charts"** — what do those steps precompute,
and which of those ideas would pay off here?

Companion doc: `performance.md` covers our pan/zoom interaction tuning in
detail; this one is about *pipeline architecture* — what gets computed when,
and what is cached where.

## The two OpenCPN preparation steps

### 1. "Rebuild chart database" — a binary catalog of chart metadata

OpenCPN never wants to open every chart file at startup just to learn where
each chart is. The chart database (`chartdbs.cpp`) is a versioned binary file
(`ChartTableHeader`, version-stamped `"V###"`) holding one `ChartTableEntry`
per chart:

- geographic extents (`LatMax/LatMin/LonMax/LonMin`) and a bounding box
- chart type, family, native scale, skew, projection
- full path + file modification date (`file_date`) and edition date
- **coverage polygons** — the chart's actual data outline (`pPlyTable`),
  auxiliary coverage areas, and *no-coverage* holes (`pNoCovrPlyTable`),
  Douglas-Peucker-simplified at build time

Staleness is detected without opening chart files: each chart directory gets a
**"magic number"** — a hash accumulated over every file's name, size, and
modification time. At startup, if a directory's hash matches the stored one,
all its entries are marked valid untouched ("No change detected on directory");
only a mismatch triggers a rescan of that directory.

### 2. "Prepare all ENC charts" — the SENC cache

This is the big one, and the reason skipping it makes OpenCPN's ENC rendering
crawl. S-57 ENC cells are ISO 8211 files: parsing one means decoding a dense
binary record structure, resolving feature→geometry pointers through shared
edge/node vector tables, and applying any sequential update files (`.001`,
`.002`, …) on top of the base cell. OpenCPN does all of that **once**, then
serializes the result as a **SENC** ("System ENC", OSENC format, `o_senc.cpp`)
— a flat, load-ready binary. A SENC stores:

| record group | contents |
|--------------|----------|
| header | cell name, edition, update number, native scale, create date |
| coverage | extents, coverage + no-coverage polygons |
| features | feature id/type records + attribute records |
| geometry | point / line / area / multipoint records, with edge indices |
| vector tables | edge and connected-node coordinate tables |
| **tessellation** | **pre-triangulated polygon vertex arrays** (`TriPrim`) ready for OpenGL |

Two details are worth stressing:

- **Polygon triangulation is done at prepare time and stored.** Area fills
  (DEPARE, LNDARE, …) render as triangle arrays with no runtime tessellation.
- **ENC updates are merged at prepare time.** The SENC embeds the update
  number it was built against; rendering never re-applies updates.

The bulk build (`senc_manager.cpp`) is a background job queue running
`nCPU − 1` concurrent `SENCBuildThread`s ("Preparing vector chart N…"), so
preparation saturates the machine without blocking the UI. Without a SENC, the
full ISO-8211 parse + update merge + tessellation happens lazily on first open
of each cell — which is exactly the slowness the user sees when the prepare
step is skipped.

### Render-time machinery (after the prep steps)

- **In-memory chart cache** (`chartdb.cpp`): opened charts live in a
  `CacheEntry` LRU with lock counts protecting charts in active use; eviction
  triggers at 80 % of a memory budget (`g_memCacheLimit`) or an entry-count
  cap. Charts can be opened `HEADER_ONLY` (metadata for lists/thumbnails) or
  `FULL_INIT` (ready to draw).
- **Render lists by priority** (`s57chart.cpp`): SENC objects are binned into
  `razRules[priority][type]` — display priority × (simplified points, paper
  points, lines, plain boundaries, symbolized boundaries) — so a frame is a
  linear walk in S-52 draw order: areas first, then lines, then points.
- **Single line-vertex buffer**: `AssembleLineGeometry()` stitches the shared
  edge/node tables into one contiguous vertex buffer for all line work, with
  per-object offsets — one upload, many draws.
- **Viewport bitmap reuse** (`DoRenderViewOnDC`): on pan, the previous frame's
  pixel buffer (`pDIB`) is shifted and only the newly exposed region is
  re-rendered (old/new viewport intersection via `rgn_last`).
- **Safety contour precompute**: each chart's DEPCNT `VALDCO` values are
  extracted and sorted once (`BuildDepthContourArray`), so selecting the
  active safety contour is a lookup, not a scan.
- **Quilting caches** (`quilt.cpp`): the composed patch list, candidate chart
  array, and covered region persist between frames; candidates come from the
  chart database's coverage polygons (including no-coverage holes), so charts
  that cannot contribute pixels are skipped without being opened.
- **Raster texture cache** (`gl_tex_cache.cpp`, not ENC but same philosophy):
  DXT1-compressed mipmap tiles of raster charts are cached to disk
  (`glTexFactory`), eliminating recompression on later runs.

## What our app does today

Our pipeline (Qt6 + GDAL + QPainter; no OpenGL, no quilting — cells of all
bands draw z-sorted):

1. **Catalog scan** (`chart_catalog.cpp`): enumerate `*.000` under the chart
   root; resolve each cell's extent **cheaply** (M_COVR layer only, no full
   parse — `computeCellExtentLonLat`); band comes from the filename's third
   character. Results are cached per root in a JSON file keyed by each file's
   **size + mtime**, so a re-scan of an unchanged set never opens a cell.
2. **Cell parse** (`chart_loader.cpp`): on demand, a worker thread opens the
   cell with GDAL's S-57 driver, reads every kept layer, projects geometry to
   Mercator metres, and captures symbology attributes. GDAL applies ENC update
   files automatically at open (driver default `UPDATES=APPLY`).
3. **Parse cache** (`feature_cache.hpp`): the parsed, projected features go
   into an in-memory LRU (256 MB / 256 entries soft budget) with on-screen
   cells pinned. Returning to a recently seen cell skips disk + GDAL entirely.
4. **Cell build** (`buildCell`, `chart_view.cpp`): per visible region, a worker
   clips features to the keep-area, simplifies to a **band-keyed tolerance**
   (`simplifyToleranceM` — ~half a pixel at the band's design scale, so zooming
   within a band never re-simplifies), resolves S-52 symbology to atlas
   indices, and emits z-sorted `BuiltPath`/`Sounding`/`BuiltSymbol` lists.
5. **Paint** (`paintEvent`): draw basemap, then per-cell paths through the
   camera transform with per-path bbox culling, then (when settled) soundings
   thinned by a spatial-hash declutter and symbols as atlas blits.
6. **Interaction LOD** (see `performance.md`): antialiasing off, point
   overlays skipped, and cell-set recomputation deferred while a gesture is in
   flight.
7. **Symbol atlas** (`sym_atlas.cpp` + `tools/gen_symbols.cpp`): the S-52
   lookup tables and sprite rectangles are baked **at compile time** into
   `symbols.bin` — even earlier than OpenCPN, which parses its presentation
   library at runtime.

## Side-by-side

| Optimization | OpenCPN | Ours |
|---|---|---|
| Chart metadata catalog on disk | Binary chart DB: extents, scale, type, **coverage + no-coverage polygons**, edition/file dates | JSON per root: extents (bbox only) + band + size/mtime — same skip-unchanged effect |
| Staleness check | Per-directory magic-number hash (name+size+mtime) | Per-file size+mtime compare |
| **Parsed-chart cache on disk** | **SENC: full post-parse binary, updates merged, geometry flat, polygons pre-triangulated** | **None — every cold cell pays GDAL ISO-8211 parse + projection** |
| Bulk preparation | "Prepare all ENC charts": background job queue, nCPU−1 threads | None (cells parse lazily, threaded, on first view) |
| In-memory chart cache | `CacheEntry` LRU, lock counts, 80 %-of-budget purge | `FeatureCache` LRU, byte+count budget, pinned on-screen cells |
| Geometry simplification | Coverage outlines DP-simplified in the DB; geometry kept full-detail and tessellated | Per-band half-pixel vertex merge at build time (cheap `drawPath`s) |
| Polygon fill strategy | Pre-tessellated triangles from the SENC (GPU-friendly) | `QPainterPath` fill per frame (CPU raster; fine at our path counts after simplification) |
| Draw organization | `razRules[priority][type]` walk in S-52 order | z-sorted `BuiltPath` list per cell (z = band×1000 + kind order) |
| Pan repaint | Previous-frame bitmap shifted; only exposed strip re-rendered | Full-widget repaint each frame (mitigated by interaction LOD) |
| Chart selection per view | Quilt: coverage-polygon-aware candidate stack, cached patches | Band targeting from visible width (+ detail bias), bbox intersection |
| Update files (.001…) | Merged once into the SENC | Re-applied by GDAL on every cold parse |
| Symbology resolution | S-52 plib parsed at runtime; per-object LUP rules cached | Baked at compile time (`symbols.bin`); resolved per feature at build |
| Raster charts | Disk cache of DXT1-compressed texture tiles | N/A (no raster chart support) |

## What's worth borrowing (ordered by payoff)

### 1. A SENC-style on-disk parse cache — our missing "prepare" step

The single biggest structural difference. Our `FeatureCache` makes *warm*
cells free, but every *cold* cell — first visit after launch, or after LRU
eviction — pays the full GDAL open + S-57 decode + update merge + projection
on a worker thread. OpenCPN pays that exactly once per chart edition.

The shape of the fix maps cleanly onto our pipeline: serialize the result of
`loadCellFeatures()` — the projected `std::vector<Feature>` (kinds, rings,
depths, object classes, symbology attrs) — to a compact binary file per cell,
keyed by the cell's size+mtime (the catalog already tracks both) plus a format
version. `dispatchLoad` then becomes: read the side file if fresh (a bulk
`fread` into vectors), else parse via GDAL and write it. Optionally add a
"Prepare all charts" action that walks the catalog through the existing
`QThreadPool` — same UX as OpenCPN's, and the natural place for a progress
dialog. Deleting the cache directory must always be safe.

Expect cold-view latency to drop from "GDAL parse" to "disk read": this is
precisely the gap users feel in OpenCPN when the prepare step is skipped.

### 2. Coverage-polygon culling (cheap, after #1)

Our catalog reduces each cell to a bounding box, but ENC footprints are often
diagonal slivers; a bbox can intersect the view while the cell contributes
nothing. OpenCPN stores the real (simplified) M_COVR polygon in its DB and
quilts against it. We already read M_COVR in `computeCellExtentLonLat` — we
just throw the ring away. Storing a DP-simplified coverage ring in the catalog
cache and testing it (not just the bbox) in `updateVisibleCells` would skip
loading/building cells whose data never reaches the screen. Helps most exactly
where it hurts most: dense harbour areas at high detail level.

### 3. Pan-repaint reuse (only if profiling says paint is the bottleneck)

OpenCPN's shifted-bitmap pan repaints only the exposed strip. Our equivalent
would be rendering the settled chart (basemap + cells, minus overlays) into a
cached `QPixmap`, blitting it shifted during a drag, and repainting only on
settle/zoom. It composes with the existing `interacting_` machinery. Worth it
on weak hardware; measure first — `performance.md`'s zoom-aware simplification
lever may make full repaints cheap enough that this never earns its
complexity.

### Already covered, no action

- **Metadata catalog + staleness** — our JSON cache with size/mtime is the
  same idea as the chart DB (per-file rather than per-directory hash; finer
  granularity, slightly more stat calls, immaterial at our chart counts).
- **In-memory LRU with protection for in-use charts** — `FeatureCache` pinning
  ≈ OpenCPN lock counts.
- **Threaded loading** — per-cell `QThreadPool` jobs ≈ the SENC job queue
  (ours is per first-view rather than bulk, which #1 would address).
- **Symbology precompute** — `symbols.bin` already goes further than OpenCPN.
- **Safety contour** — no safety-contour feature yet; if/when one is added,
  precompute the sorted VALDCO list per cell like `BuildDepthContourArray`.
- **Pre-tessellated triangles / VBOs** — only pays off on an OpenGL backend.
  Under QPainter, fewer vertices (band tolerance now, zoom-aware tolerance
  next per `performance.md`) is the equivalent lever, and it's already the
  documented plan.
