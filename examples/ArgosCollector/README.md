# ArgosCollector example

## Where to build and run tests

Run the `ArgosCollector` binary and any post-test Python helpers (`dump.py`, `trace_compare.py`, `value_compare.py`, `ui_smoke.py`) **from the CMake build output directory** for this target, **not** from the SimDB repo root.

Recommended (path may vary with your build tree):

```text
~/simdb-collector-v3/debug/examples/ArgosCollector
```

That directory holds the executable plus the `*.py` helpers (often via symlinks from the build). Writing traces and DBs there keeps `*.trace`, `*.trace.meta`, and similar outputs **out of** the repository root. Running from the repo root can litter the root with untracked metadata and make `git status` noisy.

If you need isolation, use any **temporary directory** and run from there instead, with the executable and scripts on `PATH` or copied in.
