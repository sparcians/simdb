# Argos Viewer

The Argos viewer is a desktop application for exploring and **replaying** the
data captured by the Argos collector. It opens a SimDB-produced database and
lets you step through simulated time, reconstructing the state of collected
objects at any point and visualizing them with a set of composable widgets.

The collector answers *"how do I get simulation data out fast and safely?"*; the
viewer answers *"now that it's captured, how do I look at it?"*

---

## Launching

```bash
python argos.py --database path/to/collected.db
python argos.py --database path/to/collected.db --view-file layout.avf
```

- `--database` (required): the Argos-collected database to open.
- `--view-file` (optional): an Argos View File (`*.avf`) describing a saved
  layout. If omitted, the viewer restores your last-known view when one exists.

---

## What the viewer shows

The viewer presents the simulation as a navigable **hierarchy** of the objects
that were collected, alongside the **clocks** that drove the run and the range
of simulated time that data exists for. You pick objects of interest, drop them
into visualization widgets, and scrub through time to watch them change.

Because collection is checkpoint-based (periodic full snapshots plus small
deltas in between), the viewer reconstructs an object's exact value at any
requested point in time by starting from the most recent snapshot and replaying
the deltas up to that point. This all happens behind the scenes; you just move
the playback control and the widgets update.

Databases may contain a single clock or many. When there are multiple clocks,
each object only updates on the edges of the clock it was collected against.

---

## The UI

The main window has three parts:

1. **Menu bar** -- create, open, and save views (`File > New / Open / Save View`,
   with Ctrl+N/O/S) and exit.
2. **Data inspector** -- a set of tabs, each holding a **canvas** you can split
   left/right and top/bottom to arrange multiple widgets side by side. A `+` tab
   adds new tabs. A **Logs** tab appears when the collection recorded
   notifications worth surfacing.
3. **Playback bar** -- a clock selector, the current cycle/tick readout, step
   buttons (`-30 / -10 / -3 / -1` and `+1 / +3 / +10 / +30`), and a scrubber
   slider spanning the start and end of the run.

Each widget has its own small toolbar: open its settings, clear it, split its
cell horizontally or vertically, or maximize it to fill the canvas.

### Widgets

- **Queue tables** -- a tabular view of container objects (e.g. queues and
  buffers), with configurable visible columns and per-column
  auto-colorization to make values easy to scan.
- **Queue Utilization** -- occupancy of container objects over time.
- **Scheduling Lines** -- a timeline-style view of scheduled activity.
- **Summary Views** -- aggregate/summary displays across the selection.

---

## View files (`*.avf`)

A **view file** captures your layout so you can reopen it later or share it: the
tabs, how each canvas is split, which widgets are placed where, the columns and
colorization chosen for each object, and the selected clock. Save with
`File > Save View`; the window title marks unsaved changes. The viewer also
remembers your last layout and reopens it automatically the next time you launch
without a `--view-file`.

The playback position (the tick you're currently viewing) is treated as a local,
per-session preference and is not stored in the shared view file.

---

## Relationship to the collector

The viewer is the read side of Argos; the write side is the Argos collector, a
SimDB app that streams collected data into a single database. For the collector
and an end-to-end walkthrough, see the SimDB book's Argos case study in
[`docs/book/parts/part7-argos-case-study.adoc`](../../../docs/book/parts/part7-argos-case-study.adoc).
