import argparse
import os
import sqlite3
import sys
import zlib
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from viewer.model.collection_replayers import CollectionReplaySession
from viewer.model.data_deserializers import ByteBuffer
from viewer.model.dtype_inspector import DataTypeInspector


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser("Collection semantic value comparator")
    parser.add_argument("--db-file", required=True, help="Path to Argos sqlite DB")
    parser.add_argument("--test-name", required=True, help="ArgosCollector test function name")
    return parser.parse_args()


def _require_path_cid(cids_by_path: dict[str, int], field_name: str) -> int:
    exact = [cid for path, cid in cids_by_path.items() if path == field_name]
    if len(exact) == 1:
        return exact[0]

    suffix = [cid for path, cid in cids_by_path.items() if path.endswith("." + field_name) or path.endswith("/" + field_name)]
    if len(suffix) == 1:
        return suffix[0]

    raise RuntimeError(f"Could not uniquely resolve field path for '{field_name}'")


def _replay_all_records(db_file: str):
    inspector = DataTypeInspector(db_file)
    session = CollectionReplaySession(db_file, inspector)
    replayers = session.replayers_by_cid

    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()

    cursor.execute("SELECT SerializationCID,FullPath FROM CollectableTreeNodes")
    cids_by_path = {str(path): int(cid) for cid, path in cursor.fetchall()}

    cursor.execute(
        """
        SELECT Timestamps.Timestamp, CollectionRecords.Records
        FROM CollectionRecords
        JOIN Timestamps ON Timestamps.Id = CollectionRecords.TimestampID
        ORDER BY Timestamps.Id ASC
        """
    )

    seen_times = set()
    for raw_time, compressed_blob in cursor.fetchall():
        time_point = int(raw_time) if isinstance(raw_time, str) else int(raw_time)
        seen_times.add(time_point)
        buf = ByteBuffer(zlib.decompress(compressed_blob))
        while not buf.Done():
            cid = int(buf.Read("H"))
            replayers[cid].replay_next(buf)

    conn.close()
    return replayers, cids_by_path, seen_times


def _assert_equal(name: str, actual, expected, failures: list[str]) -> None:
    if actual != expected:
        failures.append(f"{name}: expected {expected!r}, got {actual!r}")


def _validate_test_scalar_collection(db_file: str) -> int:
    replayers, cids_by_path, seen_times = _replay_all_records(db_file)

    pod_cid = _require_path_cid(cids_by_path, "pod")
    str_cid = _require_path_cid(cids_by_path, "str")
    itype_cid = _require_path_cid(cids_by_path, "itype")
    flag_cid = _require_path_cid(cids_by_path, "flag")
    inst_cid = _require_path_cid(cids_by_path, "inst")

    expected_ticks = [1, 2, 3, 4, 5, 6, 100]
    missing_ticks = [t for t in expected_ticks if t not in seen_times]
    if missing_ticks:
        raise RuntimeError(f"Missing expected timestamps in DB: {missing_ticks}")

    expected_pod = {1: 4, 2: 5, 3: 6, 4: 7, 5: 8, 6: 9}
    expected_str = {1: "foo", 2: "bar", 3: "fiz", 4: "biz", 5: "fuz", 6: "buz"}
    expected_itype = {1: "MEM", 2: "NO_OP", 3: "MEM", 4: "ILLEGAL", 5: "ILLEGAL", 6: "CSR"}
    expected_flag = {1: True, 2: False, 3: True, 4: False, 5: True, 6: False}

    failures: list[str] = []

    for tick in range(1, 7):
        _assert_equal(f"tick {tick} pod", replayers[pod_cid].GetDataValueAtTime(tick), expected_pod[tick], failures)
        _assert_equal(f"tick {tick} str", replayers[str_cid].GetDataValueAtTime(tick), expected_str[tick], failures)
        _assert_equal(
            f"tick {tick} itype",
            replayers[itype_cid].GetDataValueAtTime(tick),
            expected_itype[tick],
            failures,
        )
        _assert_equal(f"tick {tick} flag", replayers[flag_cid].GetDataValueAtTime(tick), expected_flag[tick], failures)

    inst_t1 = replayers[inst_cid].GetDataValueAtTime(1)
    inst_t2 = replayers[inst_cid].GetDataValueAtTime(2)
    inst_t6 = replayers[inst_cid].GetDataValueAtTime(6)
    inst_t100 = replayers[inst_cid].GetDataValueAtTime(100)

    if not isinstance(inst_t1, dict) or not inst_t1:
        failures.append(f"tick 1 inst expected non-empty struct, got {inst_t1!r}")
    if inst_t2 != inst_t1:
        failures.append(f"tick 2 inst should equal tick 1 inst (forced CARRY), got {inst_t2!r} vs {inst_t1!r}")
    if inst_t100 != inst_t6:
        failures.append(f"tick 100 inst should equal tick 6 inst, got {inst_t100!r} vs {inst_t6!r}")

    if failures:
        print("VALUE MISMATCH")
        for msg in failures[:20]:
            print(f"  - {msg}")
        if len(failures) > 20:
            print(f"  ... and {len(failures) - 20} more")
        return 1

    print("VALUE MATCH")
    print("  test: TestScalarCollection")
    print("  checked ticks: 1-6,100")
    print("  checked fields: pod,str,itype,flag,inst")
    return 0


def main() -> int:
    args = _parse_args()
    if not os.path.exists(args.db_file):
        raise RuntimeError(f"DB file does not exist: {args.db_file}")

    if args.test_name == "TestScalarCollection":
        return _validate_test_scalar_collection(args.db_file)

    print(f"Skipping value comparison for {args.test_name} (not implemented yet)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
