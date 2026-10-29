#!/usr/bin/env python3
"""Convert a gcovr JSON summary into shields.io endpoint badge files.

Usage:
    gcovr ... --json-summary cov.json --json-summary-pretty
    python3 scripts/coverage-report.py cov.json --out-dir badges

One file is written per (directory, metric), named
simdb-cov-<directory>-<metric>.json, e.g. simdb-cov-apps-function.json. The
README embeds them via https://img.shields.io/endpoint?url=<raw gist url>.
"""

import argparse
import json
from pathlib import Path

DIRECTORIES = ["apps", "pipeline", "schema", "sqlite", "utils"]
METRICS = [("function", "function"), ("line", "line"), ("dc", "branch")]
SOURCE_ROOT = "include/simdb/"

# (minimum percent, shields.io color), checked in order
COLORS = [
    (90, "brightgreen"),
    (80, "green"),
    (70, "yellowgreen"),
    (60, "yellow"),
    (50, "orange"),
    (0, "red"),
]


def aggregate(summary):
    totals = {d: {key: [0, 0] for _, key in METRICS} for d in DIRECTORIES}
    for entry in summary["files"]:
        name = entry["filename"].replace("\\", "/")
        if SOURCE_ROOT not in name:
            continue
        directory = name.split(SOURCE_ROOT, 1)[1].split("/", 1)[0]
        if directory not in totals:
            continue
        for _, key in METRICS:
            totals[directory][key][0] += entry[f"{key}_covered"]
            totals[directory][key][1] += entry[f"{key}_total"]
    return totals


def badge(label, covered, total):
    if total == 0:
        return {"schemaVersion": 1, "label": label, "message": "n/a", "color": "lightgrey"}
    percent = 100.0 * covered / total
    color = next(c for minimum, c in COLORS if percent >= minimum)
    return {"schemaVersion": 1, "label": label, "message": f"{percent:.1f}%", "color": color}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("summary", type=Path, help="gcovr --json-summary output")
    parser.add_argument("--out-dir", type=Path, default=Path("badges"))
    args = parser.parse_args()

    totals = aggregate(json.loads(args.summary.read_text()))
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for directory in DIRECTORIES:
        for metric, key in METRICS:
            covered, total = totals[directory][key]
            path = args.out_dir / f"simdb-cov-{directory}-{metric}.json"
            path.write_text(json.dumps(badge(metric, covered, total)) + "\n")
            print(f"{path.name}: {covered}/{total}")


if __name__ == "__main__":
    main()
