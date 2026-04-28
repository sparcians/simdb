import argparse
import sys
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from collection_trace_compare import run_value_compare


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser("Collection generic framing comparator")
    parser.add_argument("--db-file", required=True, help="Path to Argos sqlite DB")
    parser.add_argument(
        "--sim-trace-file",
        default=None,
        help="Optional .trace file from C++ side to compare against",
    )
    parser.add_argument(
        "--allow-sim-only-records",
        action="store_true",
        help="Allow extra records in sim trace (pre-dedup) while preserving order",
    )
    parser.add_argument(
        "--max-records",
        type=int,
        default=None,
        help="Stop after parsing this many records (across all timestamps); for large-DB smoke tests",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    return run_value_compare(
        db_file=args.db_file,
        sim_trace_file=args.sim_trace_file,
        allow_sim_only_records=args.allow_sim_only_records,
        max_records=args.max_records,
    )


if __name__ == "__main__":
    raise SystemExit(main())
