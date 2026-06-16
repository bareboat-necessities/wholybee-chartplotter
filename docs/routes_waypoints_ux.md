# Routes & Waypoints — UX Review and Proposal

Status: proposal (no code changed). Scope: make creating, viewing, and editing
routes and waypoints easier and more intuitive, reusing patterns the app already
has. Functionality is complete; this is about flow and discoverability.

---

## 1. How it works today

Everything is reached through **Menu → Routes and Waypoints**, a sub-page with
seven items ([src/side_menu.cpp](../src/side_menu.cpp) `buildRoutesPage`):

| Section | Items |
|---|---|
| Routes | Create Route · Edit Route · List Routes |
| Waypoints | Create Waypoint · Edit Waypoint · Drop Waypoint · List Waypoints |

Typical flows ([src/main_window.cpp](../src/main_window.cpp)):

- **Create Route** → menu closes → floating bar (*"Tap to add points · drag to
  move · tap a point to select"*, Complete / Delete Point / Cancel) → tap to
  append, drag to move, Complete → **name dialog** → saved.
- **Edit Route** → opens the route list in *pick* mode → pick → chart fits →
  same edit bar.
- **Create Waypoint** → menu closes → tap chart → **name dialog** → saved.
- **Drop Waypoint** → waypoint at ownship → **name dialog**.
- **Edit Waypoint** → waypoint list in *pick* mode → pick → chart fits → drag →
  Done.
- **List Routes / List Waypoints** → scrollable list with a Visible checkbox,
  row-select, **Delete**, and **Properties** (name/description + per-point
  lat/lon editing, with a per-point "Edit" that drags on the chart).

## 2. Where it feels clumsy

1. **Routes and waypoints aren't tappable on the chart.** The overlay declines
   all clicks unless it is already in an edit mode
   ([src/route_overlay.cpp](../src/route_overlay.cpp) `hitTest` returns `false`
   when `mode_ == None`). To do *anything* to an existing object you must open
   the menu, pick a list, find the row, and choose an action. Meanwhile **AIS
   targets are directly tappable** (tap → quick-info popup → second tap opens the
   full window — `MainWindow::showAisTarget`, `AisQuickInfoWindow`). Routes and
   waypoints should behave the same way.

2. **Seven menu items with overlapping jobs.** "Edit Route" and "List Routes →
   pick" both lead to editing. "Edit Waypoint" overlaps "List Waypoints →
   Properties / drag". The list dialogs are already management hubs, so most of
   the separate verbs are redundant.

3. **Mode-first creation.** You must choose "Create Waypoint" *before* you can
   place one. A chart app wants the reverse: point first, then act
   (long-press → "New waypoint here").

4. **The name dialog interrupts every creation.** Placing a waypoint or finishing
   a route immediately blocks on a modal name prompt — awkward when you're
   dropping several marks quickly.

5. **Two places to edit a route, split by capability.** Coordinates are typed in
   *Properties*; positions are dragged in *Edit Route*. They're reached by
   different paths and neither mentions the other.

6. **No persistent selection / feedback off-chart.** Outside an edit session
   there's no notion of a "selected" route or waypoint, so there's no obvious
   place to hang contextual actions.

7. **Route drawing is bare.** You can append, drag, and delete the selected
   node, but there's no rubber-band line to the cursor, no undo of the last
   point, and no way to insert a point into the middle of a leg.

## 3. Proposed changes

Ordered by value-to-effort. P1 items remove most of the friction on their own.

### P1 — Make objects tappable on the chart (the big one)

Extend `RouteOverlay::hitTest` to also pick when `mode_ == None`: hit-test
waypoints and route legs/nodes and report the hit up to `MainWindow` via a
callback (mirror `AisOverlay::setOnTargetClicked`). On a hit, show a small
**quick-info / context popup** anchored at the tap — reuse the
`AisQuickInfoWindow` pattern — with the object's name and the actions that today
require menu trips:

- **Waypoint popup:** Name · position · `Edit on chart` (drag) · `Properties` ·
  `Hide` · `Delete`.
- **Route popup:** Name · # points · `Edit on chart` · `Properties` · `Hide` ·
  `Delete` · `Reverse` (nice-to-have).

This collapses "open menu → pick list → find row → act" into one tap, and makes
routes/waypoints consistent with AIS. The drag/Properties/visibility/delete code
all already exists; this is mostly wiring an existing hit-test to existing
actions.

### P1 — Long-press empty chart to create

Add a long-press (press-and-hold) recognizer in `ChartView` (alongside the
existing press/drag handling). On empty water it opens a tiny context menu:
**New waypoint here** · **Start route here**. This removes the "pick a mode
first" step for the common case and is the touch-native gesture.

### P2 — Consolidate the sub-menu

Let the lists be the hub and drive editing from the chart. Seven items → four:

| Today | Proposed |
|---|---|
| Create Route | **New Route** (start drawing) |
| Edit Route | *(removed — tap the route, or List → Edit)* |
| List Routes | **Routes…** (list hub: New, per-row Edit/Properties/Visible/Delete) |
| Create Waypoint | *(removed — long-press chart → New waypoint here)* |
| Edit Waypoint | *(removed — tap the waypoint, or List → Edit)* |
| Drop Waypoint | **Drop Waypoint at Boat** |
| List Waypoints | **Waypoints…** (list hub) |

Each list row gains an inline **Edit** (chart drag) and **Properties** action so
the list is a complete manager, and "pick mode" becomes the normal mode.

### P2 — Defer naming; auto-name with easy rename

Save new objects immediately with an auto-name (`Waypoint 7`, `Route 3`) instead
of a blocking prompt, and surface a quick **rename** in the quick-info popup and
Properties. Keep an *optional* "prompt for name on create" setting for users who
prefer it. This keeps rapid mark-dropping fluid.

### P3 — Better route drawing

- **Rubber-band**: draw a faint segment from the last placed point to the cursor
  while creating (`RouteOverlay::paint` already has the working route; add a
  cursor point fed from `mouseMove`).
- **Undo last point** button on the edit bar.
- **Insert on a leg**: tapping a route segment in edit mode inserts a node there
  (instead of only appending to the end).
- **Clearer edit bar**: icons + a live point count; "Undo" next to "Delete
  Point".

### P3 — Unify Edit + Properties

Make **Properties** the single editor (it already lists points with per-point
"Edit on chart"), and reach on-chart dragging from the quick-info popup's *Edit
on chart*. Drop the standalone "Edit Route/Waypoint" verbs. One mental model:
*select the object → Properties to type, Edit on chart to drag.*

## 4. Suggested interaction model (after)

- **Tap** a waypoint/route on the chart → quick-info popup with contextual
  actions.
- **Long-press** empty chart → New waypoint here / Start route here.
- **Menu → Routes and Waypoints** → New Route · Drop Waypoint at Boat · Routes… ·
  Waypoints… (lists are the full managers).
- **Selected object** is highlighted on the chart; the floating bar shows the
  actions for the current task (draw / drag / done).

## 5. Implementation roadmap

1. **Chart picking + quick-info** (P1): extend `RouteOverlay::hitTest` for the
   non-edit case + a `onObjectClicked` callback; build a routes/waypoints
   quick-info popup modeled on `AisQuickInfoWindow`; route its buttons to the
   existing edit/properties/visibility/delete paths.
2. **Long-press create** (P1): gesture in `ChartView` → context menu → existing
   create paths.
3. **Menu consolidation + lists as hubs** (P2): trim `buildRoutesPage`; add
   per-row Edit/Properties to the list dialogs.
4. **Auto-name + rename** (P2).
5. **Drawing niceties + Edit/Properties unification** (P3).

Steps 1–2 deliver most of the perceived improvement and are low-risk because they
reuse the AIS interaction pattern and the existing route/waypoint actions.

## 6. Open questions

- **Naming:** auto-name + later rename (recommended) vs. keep the create-time
  prompt (perhaps as a setting)?
- **Quick-info depth:** a compact action popup (recommended) vs. a fuller
  info window like the AIS target window?
- **Long-press vs. on-screen "＋" button:** long-press is touch-native; a visible
  add button is more discoverable. Could ship both.
- **Selection scope:** should tapping a route also let you tap individual legs to
  insert/split, or keep leg-editing inside an explicit edit mode?
