import argparse
import sys
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from collection_trace_compare import run_trace_compare


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser("Collection byte trace comparator")
    parser.add_argument("--db-file", required=True, help="Path to Argos sqlite DB")
    parser.add_argument(
        "--ui-trace-file",
        default="simdb_collection_bytes.ui",
        help="Output file for Python-interpreted byte trace",
    )
    parser.add_argument(
        "--sim-trace-file",
        default=None,
        help="Optional .trace file from C++ side to compare against",
    )
    parser.add_argument(
        "--cid",
        type=int,
        default=None,
        help="Optional CID filter (emit only records for this collectable)",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    return run_trace_compare(args.db_file, args.ui_trace_file, args.sim_trace_file, args.cid)


if __name__ == "__main__":
    raise SystemExit(main())
