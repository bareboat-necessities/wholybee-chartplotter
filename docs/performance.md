# Rendering Performance & the Detail Level

Notes on what makes the chart slow at high **Detail Level**, what has already
been done, and which levers remain. Written after the first round of
pan/zoom optimisation so a later pass has a map to work from.

All line references are to `src/chart_view.cpp` unless noted.

## Why detail costs performance

The Detail Level slider biases which ENC usage bands are drawn for a given
zoom. `computeViewBoxes()` scales the visible width by `pow(4, -level)` before
choosing the target band, so `+1` pulls in roughly one band finer, `+2` two
bands finer — **without changing the zoom** (`ppm_`).

The consequence: at a positive detail level you load and draw *fine-band*
cells (harbour/approach geometry, dense soundings, many buoys) while viewing
them at a *coarse* on-screen scale. Every per-frame cost multiplies:

```
              cells loaded      geometry per cell     points per cell
 detail 0      coastal only      coarse (band-3 tol)   sparse
 detail +2     + approach        fine (band-5 tol,     dense (soundings
               + harbour          ~thousands of         on top of each
                                   vertices/path)        other)
```

## The two per-frame costs

`paintEvent()` redraws everything every frame during a gesture. The cost
splits in two:

1. **Point overlays** — every sounding is a `QPainter::drawText` (CPU glyph
   rasterisation, expensive) and every symbol is a pixmap blit. At `+1`/`+2`
   there are thousands of each. Historically the single biggest cost.

2. **Vector geometry** — one `drawPath()` per visible `BuiltPath`. Each cell
   is simplified to **its own band's** half-pixel tolerance, not the actual
   zoom (`simplifyToleranceM()`, ~line 99). A band-5 cell viewed zoomed-out
   carries far more vertices than the screen can resolve. This is the
   **root cause** of the residual geometry cost and is not yet addressed.

## Interaction model (the LOD hook)

Three flags/timers cooperate. New optimisations should hang off this same
mechanism rather than inventing a parallel one.

| Name | Interval | Role |
|------|----------|------|
| `interacting_` | — | true while a pan/zoom gesture is in flight |
| `aaTimer_` | 180 ms single-shot | "gesture settled": clears `interacting_`, repaints, catches up deferred work |
| `updateTimer_` | 120 ms single-shot | debounced cell-set recompute (`updateVisibleCells`) |

`beginInteraction()` sets `interacting_ = true` and (re)starts `aaTimer_`.
Each mouse-move / wheel step restarts both timers, so during a **fast**
continuous gesture neither fires; they only fire once motion pauses.

## What has already been done

1. **Antialiasing off during interaction** (pre-existing) —
   `p.setRenderHint(QPainter::Antialiasing, !interacting_)`.

2. **Skip point overlays during interaction** — the soundings/symbols block is
   gated on `pointLodVisible_ && !interacting_`. The moving frame draws only
   vector geometry; labels/symbols snap back on settle. This removed the
   dominant cost from the moving frame.

3. **Defer cell management during interaction** — `updateVisibleCells()` +
   `maybeBuildBasemap()` are skipped while `interacting_` is true and run once
   when `aaTimer_` fires. This fixed the **start-of-pan stall**: the 120 ms
   `updateTimer_` was firing *between* moves on a slow drag-start and running
   the heavy cell-set recompute synchronously on the GUI thread. Fast panning
   starved the timer, which is why the stall only showed at the start.

   Tradeoff: on a **long, slow continuous drag** the view can pan past its
   pre-loaded margin (`keepArea` = 1.5× the viewport, set in
   `computeViewBoxes`) and briefly show blank until motion pauses and cells
   reload. Normal pans stay within the margin.

### Sounding declutter (related, not a gesture optimisation)

Independently, soundings are thinned by a greedy minimum-screen-gap filter at
paint time (`soundingMinSpacing()` + the spatial-hash loop in the soundings
block). The gap grows with detail level so dense soundings don't pile up. This
reduces *draw* count at high detail but only runs on the settled frame (points
are skipped while interacting).

## Remaining levers (roughly highest payoff first)

### 1. Zoom-aware geometry simplification — the big one

**Problem:** `simplifyToleranceM(band)` keys the vertex-merge tolerance to the
cell's band, not the viewing zoom. At detail `+2` a band-5 cell is drawn with
harbour-grade vertex density at a coastal on-screen scale.

**Idea:** bias the build tolerance by the detail level (known at build time).
Each `+1` views cells ~one band coarser, so multiply the tolerance by
`pow(4, level)` (≈ the per-band tolerance ratio). At detail 0 → ×1 (unchanged);
`+2` → ×16 → far fewer vertices → dramatically cheaper `drawPath`. This helps
the start frame, the moving frames, and zoom alike — it attacks vertex count
directly instead of masking it.

**Why it's invasive:** cells are simplified **once per band and cached**
(`simplifyToleranceM`'s comment spells out the assumption), and are only
rebuilt on pan when their `clipBox` stops containing the view — **not on zoom
or detail change**. To make tolerance depend on detail you must force loaded
cells to rebuild when the level changes:

- In `setChartDetailLevel()` (after storing the new level) drop the built
  cells — clear `loaded_` / `building_` / `inFlight_` bookkeeping — so the
  subsequent `scheduleUpdate()` re-clips and re-simplifies at the new
  tolerance.
- Thread the effective tolerance through `dispatchBuild()` →
  `buildCell(..., tol, ...)`. `buildCell` already takes `tol` as a parameter,
  so the change is localised to how the caller computes it.

**Risk areas to verify:** the 180° wrap-seam clipping (per-cell `drawOffsetX`
and the `shiftX(keepArea, -off)` real-frame clip), and that a full rebuild on
each detail change doesn't visibly flash. Test across the date line and with
cells from several bands in view at once.

### 2. Spatial index for path culling

`drawPaths` tests `bp.bounds.intersects(visFrame)` for **every** path in every
loaded cell each frame. With many fine cells that per-path test alone adds up.
A coarse per-cell grid / R-tree of path bounds would let the paint skip whole
cells and clusters. Lower payoff than (1) and only worth it if profiling shows
the cull loop itself (not `drawPath`) is hot.

### 3. Cap cells pulled in by detail bias

If the geometry fix isn't enough, limit how many bands the bias can add (e.g.
clamp `maxBand` to `target + N`) so `+2` over a dense harbour region doesn't
load an unbounded pile of cells. This trades completeness for predictability;
keep it as a safety valve, not a primary fix.

### 4. Periodic mid-drag refresh (only if long-drag blanking annoys)

The deferral in "what's been done" #3 means a long slow drag can outrun the
pre-loaded margin. If that becomes a problem, allow `updateVisibleCells()` to
run during interaction at a coarse cadence (e.g. every Nth move or a separate
longer timer) — but keep it off the critical "slow start" path that caused the
original stall.

## Where to measure first

Before implementing (1), confirm the split with a quick frame timer around the
two blocks in `paintEvent`:

- the vector `drawPaths` loops (cells), and
- the point-overlay block.

If vector drawing dominates the moving frame at `+2`, go straight to
zoom-aware simplification (lever 1). If the cull loop dominates over the actual
`drawPath` calls, lever 2 moves up the list.
