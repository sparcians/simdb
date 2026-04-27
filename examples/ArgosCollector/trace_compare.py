import argparse
import os
import sqlite3
import struct
import sys
import zlib
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from viewer.model.collection_replayers import CreateReplayersByCID
from viewer.model.data_deserializers import ByteBuffer
from viewer.model.dtype_inspector import DataTypeInspector


_ACTION_NAMES = {
    0: "DISABLED",
    1: "ENABLED",
    2: "QUIETED",
    3: "AWAKENED",
    4: "FULL",
    5: "CARRY",
    6: "ACTION_6",
    7: "ACTION_7",
    8: "ACTION_8",
    9: "ACTION_9",
}


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


def _decode_action(raw_blob: bytes, payload_start_idx: int) -> int:
    if payload_start_idx >= len(raw_blob):
        raise RuntimeError("Malformed record: missing action byte")
    return struct.unpack("B", raw_blob[payload_start_idx : payload_start_idx + 1])[0]


def _emit_ui_trace(db_file: str, out_path: str, selected_cid: int | None) -> None:
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    replayers_by_cid = CreateReplayersByCID(conn, inspector=inspector, db_file=db_file)

    with open(out_path, "w", encoding="utf-8") as out:
        out.write("Bytes\tDescription\n")

        cursor.execute("SELECT TimestampID,Records FROM CollectionRecords ORDER BY TimestampID ASC")
        for _timestamp_id, compressed_blob in cursor.fetchall():
            raw_blob = zlib.decompress(compressed_blob)
            blob_buf = ByteBuffer(raw_blob)

            while not blob_buf.Done():
                cid = int(blob_buf.Read("H"))
                if selected_cid is not None and cid != selected_cid:
                    # Keep parser state synchronized by replaying this payload.
                    replayers_by_cid[cid].replay_next(blob_buf)
                    continue

                out.write("2\tcid\n")

                payload_start_idx = blob_buf._read_idx
                action = _decode_action(raw_blob, payload_start_idx)
                _ = _ACTION_NAMES.get(action, f"ACTION_{action}")
                out.write("1\taction\n")

                before = blob_buf._read_idx
                replayers_by_cid[cid].replay_next(blob_buf)
                consumed_payload = blob_buf._read_idx - before
                if consumed_payload < 1:
                    raise RuntimeError(f"CID {cid}: replay consumed invalid payload length {consumed_payload}")

                trailing = consumed_payload - 1
                if trailing > 0:
                    out.write(f"{trailing}\tbytes\n")


def _read_trace_rows(path: str) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    with open(path, "r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            if i == 0:
                continue
            line = line.strip()
            if not line:
                continue
            parts = line.split("\t", 1)
            if len(parts) != 2:
                raise RuntimeError(f"Malformed trace row in {path!r}: {line!r}")
            rows.append((parts[0], parts[1]))
    return rows


def _compare_traces(sim_trace: str, ui_trace: str) -> int:
    sim_rows = _read_trace_rows(sim_trace)
    ui_rows = _read_trace_rows(ui_trace)

    max_common = min(len(sim_rows), len(ui_rows))
    for idx in range(max_common):
        if sim_rows[idx] != ui_rows[idx]:
            print("TRACE DIVERGENCE")
            print(f"  row: {idx + 2}")
            print(f"  sim: {sim_rows[idx][0]!r}\t{sim_rows[idx][1]!r}")
            print(f"  ui : {ui_rows[idx][0]!r}\t{ui_rows[idx][1]!r}")
            return 1

    if len(sim_rows) != len(ui_rows):
        print("TRACE LENGTH MISMATCH")
        print(f"  sim rows: {len(sim_rows)}")
        print(f"  ui  rows: {len(ui_rows)}")
        return 1

    print("TRACE MATCH")
    print(f"  rows compared: {len(sim_rows)}")
    return 0


def main() -> int:
    args = _parse_args()
    if not os.path.exists(args.db_file):
        raise RuntimeError(f"DB file does not exist: {args.db_file}")

    _emit_ui_trace(args.db_file, args.ui_trace_file, args.cid)
    print(f"Wrote UI trace: {args.ui_trace_file}")

    if args.sim_trace_file is not None:
        if not os.path.exists(args.sim_trace_file):
            raise RuntimeError(f"SIM trace does not exist: {args.sim_trace_file}")
        return _compare_traces(args.sim_trace_file, args.ui_trace_file)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
