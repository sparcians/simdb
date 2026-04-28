import argparse
from collections import Counter
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

from viewer.model.data_deserializers import ByteBuffer
from viewer.model.dtype_inspector import DataTypeInspector

from trace_db_common import (
    consume_payload_and_count_tail,
    decode_action,
    load_layouts,
)
from trace_format import read_trace_rows


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


def _emit_ui_trace(db_file: str, out_path: str, selected_cid: int | None) -> None:
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    layouts_by_cid = load_layouts(conn, inspector)
    last_sent_after_cid_by_cid: dict[int, int] = {}

    with open(out_path, "w", encoding="utf-8") as out:
        out.write("Bytes\tDescription\n")

        cursor.execute("SELECT TimestampID,Records FROM CollectionRecords ORDER BY TimestampID ASC")
        for _timestamp_id, compressed_blob in cursor.fetchall():
            raw_blob = zlib.decompress(compressed_blob)
            blob_buf = ByteBuffer(raw_blob)

            while not blob_buf.Done():
                cid = int(blob_buf.Read("H"))
                layout = layouts_by_cid[cid]
                payload_start_idx = blob_buf._read_idx
                action = decode_action(raw_blob, payload_start_idx)

                blob_buf.Read("B")
                trailing_chunks = consume_payload_and_count_tail(
                    blob_buf, action, layout, last_sent_after_cid_by_cid
                )

                if action >= 4:
                    # last_sent_bytes_ in C++ stores full record bytes, and lifecycle
                    # replay appends everything after cid: [action + payload_tail].
                    last_sent_after_cid_by_cid[cid] = 1 + sum(trailing_chunks)

                if selected_cid is not None and cid != selected_cid:
                    continue

                out.write("2\tcid\n")
                out.write("1\taction\n")
                for nbytes in trailing_chunks:
                    out.write(f"{nbytes}\tbytes\n")


def _read_trace_rows(path: str) -> list[tuple[str, str]]:
    return read_trace_rows(
        path,
        allowed_descriptions={"cid", "action", "bytes", "heartbeat replay bytes", "lifecycle payload tail"},
    )


def _compare_traces(sim_trace: str, ui_trace: str) -> int:
    sim_rows = _read_trace_rows(sim_trace)
    ui_rows = _read_trace_rows(ui_trace)

    def _rows_to_record_totals(rows: list[tuple[str, str]]) -> list[tuple[int, list[tuple[str, str]]]]:
        records: list[tuple[int, list[tuple[str, str]]]] = []
        cur_rows: list[tuple[str, str]] = []
        cur_total = 0
        for row in rows:
            if row == ("2", "cid"):
                if cur_rows:
                    records.append((cur_total, cur_rows))
                    cur_rows = []
                    cur_total = 0
            else:
                cur_total += int(row[0])
            cur_rows.append(row)
        if cur_rows:
            records.append((cur_total, cur_rows))
        return records

    sim_records = _rows_to_record_totals(sim_rows)
    ui_records = _rows_to_record_totals(ui_rows)

    sim_totals = Counter(total for total, _ in sim_records)
    ui_totals = Counter(total for total, _ in ui_records)

    for total, ui_count in ui_totals.items():
        sim_count = sim_totals.get(total, 0)
        if ui_count > sim_count:
            print("TRACE DIVERGENCE")
            print(f"  missing record signature total-bytes-after-cid={total}")
            print(f"  ui count:  {ui_count}")
            print(f"  sim count: {sim_count}")
            return 1

    tolerated_records = sum(sim_totals.values()) - sum(ui_totals.values())
    tolerated_rows = 0
    if tolerated_records > 0:
        # Approximation for reporting: average rows per tolerated record is not
        # important for pass/fail, only for debugging context.
        tolerated_rows = sum(len(rows) for _, rows in sim_records) - sum(len(rows) for _, rows in ui_records)

    if tolerated_records > 0:
        print("TRACE MATCH (sim has filtered pre-dedup records)")
        print(f"  records compared: {len(ui_records)}")
        print(f"  tolerated sim-only records: {tolerated_records}")
        print(f"  tolerated sim-only rows: {tolerated_rows}")
    else:
        print("TRACE MATCH")
        print(f"  records compared: {len(sim_records)}")
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
