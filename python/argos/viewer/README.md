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
python argos.py --database path/to/collected.db --layout-file layout.alf
```

- `--database` (required): the Argos-collected database to open.
- `--layout-file` (optional): an Argos Layout File (`*.alf`) describing a saved
  layout. If supplied, this view is always used and the database's last-known
  view is ignored.

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

1. **Menu bar** -- create, open, and save layouts (`File > New / Open / Save Layout`,
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

## Layout files (`*.alf`)

A **layout file** captures your layout so you can reopen it later or share it: the
tabs, how each canvas is split, which widgets are placed where, the columns and
colorization chosen for each object, and the selected clock. Save with
`File > Save Layout`; the window title marks unsaved changes. When closing an
unnamed dirty layout, Argos asks whether to save it as an ALF file, save it into
the database, discard it, or cancel closing. A view saved into the database is
restored automatically the next time that database is opened without a
`--layout-file`.

The playback position (the tick you're currently viewing) is treated as a local,
per-session preference and is not stored in the shared layout file.

---

## Relationship to the collector

The viewer is the read side of Argos; the write side is the Argos collector, a
SimDB app that streams collected data into a single database. For the collector
and an end-to-end walkthrough, see the SimDB book's Argos case study in
[`docs/book/parts/part7-argos-case-study.adoc`](../../../docs/book/parts/part7-argos-case-study.adoc).
