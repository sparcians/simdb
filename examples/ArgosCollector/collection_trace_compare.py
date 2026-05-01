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
    DbRecordFrame,
    consume_payload_and_count_tail,
    decode_action,
    hex_window,
    load_layouts,
)
from trace_format import read_trace_rows

def _action_trace_value(action: int, mode: str) -> str:
    """Match C++ naming for lifecycle + minifier action bytes."""
    if action < 4:
        return ("DISABLED", "ENABLED", "QUIETED", "AWAKENED")[action]
    if mode == "scalar":
        return {4: "FULL", 5: "CARRY"}.get(action, str(action))
    if mode == "contig":
        return {4: "FULL", 5: "CARRY", 6: "SWAP", 7: "ARRIVE", 8: "DEPART", 9: "BOOKENDS"}.get(action, str(action))
    if mode == "sparse":
        return {4: "FULL", 5: "CARRY", 6: "EXCHANGE", 7: "REMOVE"}.get(action, str(action))
    return str(action)


def _iter_ui_trace_records(db_file: str, selected_cid: int | None):
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    layouts_by_cid = load_layouts(conn, inspector)
    last_full_payload_bytes_by_cid: dict[int, int] = {}

    try:
        cursor.execute("SELECT Id,Timestamp FROM Timestamps")
        times_by_id = {}
        for _id, _timestamp in cursor.fetchall():
            times_by_id[_id] = _timestamp

        cursor.execute("SELECT TimestampID,Records FROM CollectionRecords ORDER BY TimestampID ASC")
        for _timestamp_id, compressed_blob in cursor.fetchall():
            raw_blob = zlib.decompress(compressed_blob)
            blob_buf = ByteBuffer(raw_blob)
            time_str = str(times_by_id[_timestamp_id])

            while not blob_buf.Done():
                cid = int(blob_buf.Read("H"))
                layout = layouts_by_cid[cid]
                payload_start_idx = blob_buf._read_idx
                action = decode_action(raw_blob, payload_start_idx)

                blob_buf.Read("B")
                trailing_chunks = consume_payload_and_count_tail(
                    blob_buf, action, layout, last_full_payload_bytes_by_cid
                )

                if action == 4:
                    # Lifecycle re-entry appends only FULL payload bytes (no prior action).
                    last_full_payload_bytes_by_cid[cid] = sum(trailing_chunks)

                if selected_cid is not None and cid != selected_cid:
                    continue

                yield (time_str, cid, _action_trace_value(action, layout.mode), trailing_chunks)
    finally:
        conn.close()


def _write_ui_record(out, time_str: str, cid: int, action_label: str, trailing_chunks: list[int]) -> None:
    out.write(f"record at time {time_str}\n")
    out.write(f"  2 bytes, cid, value {cid}\n")
    out.write(f"  1 byte, action, value {action_label}\n")
    for nbytes in trailing_chunks:
        unit = "byte" if nbytes == 1 else "bytes"
        out.write(f"  {nbytes} {unit}, bytes\n")


def _ui_record_rows(trailing_chunks: list[int]) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = [("2", "cid"), ("1", "action")]
    rows.extend((str(nbytes), "bytes") for nbytes in trailing_chunks)
    return rows


def emit_ui_trace(db_file: str, out_path: str, selected_cid: int | None) -> None:
    with open(out_path, "w", encoding="utf-8") as out:
        for time_str, cid, action_label, trailing_chunks in _iter_ui_trace_records(db_file, selected_cid):
            _write_ui_record(out, time_str, cid, action_label, trailing_chunks)


def emit_ui_trace_lockstep(
    db_file: str, out_path: str, sim_trace_file: str, selected_cid: int | None
) -> int:
    sim_rows = _read_filtered_trace_rows(sim_trace_file)
    sim_records = _rows_to_record_totals(sim_rows)
    matched_sim_records: set[int] = set()
    ui_record_idx = 0

    def _record_matches(
        ui_rows: list[tuple[str, str]],
        sim_record_rows: list[tuple[str, str]],
    ) -> bool:
        if len(sim_record_rows) < 2:
            return False
        if sim_record_rows[0] != ("2", "cid"):
            return False
        if sim_record_rows[1] != ("1", "action"):
            return False

        sim_pos = 2
        for row_idx, ui_row in enumerate(ui_rows):
            if row_idx < 2:
                if sim_pos - 2 + row_idx >= len(sim_record_rows):
                    return False
                if sim_record_rows[row_idx] != ui_row:
                    return False
                continue

            target_bytes = int(ui_row[0])
            consumed = 0
            while consumed < target_bytes:
                if sim_pos >= len(sim_record_rows):
                    return False
                consumed += int(sim_record_rows[sim_pos][0])
                sim_pos += 1
            if consumed != target_bytes:
                return False

        # If SIM record has remaining payload bytes, UI under-consumed this record.
        return sim_pos == len(sim_record_rows)

    with open(out_path, "w", encoding="utf-8") as out:
        for time_str, cid, action_label, trailing_chunks in _iter_ui_trace_records(db_file, selected_cid):
            _write_ui_record(out, time_str, cid, action_label, trailing_chunks)
            ui_rows = _ui_record_rows(trailing_chunks)

            matched = False
            for sim_i, (_, sim_record_rows) in enumerate(sim_records):
                if sim_i in matched_sim_records:
                    continue
                if _record_matches(ui_rows, sim_record_rows):
                    matched = True
                    matched_sim_records.add(sim_i)
                    break

            if not matched:
                print("FIRST DIVERGENCE")
                print("  could not find matching sim record for emitted ui record")
                print(f"  ui record-index: {ui_record_idx}")
                print(f"  time: {time_str}")
                print(f"  cid: {cid}")
                print(f"  action: {action_label}")
                print(f"  ui rows: {ui_rows}")
                print(f"  sim records scanned: {len(sim_records)}")
                return 1

            ui_record_idx += 1

    # Any unmatched SIM records are tolerated sim-only (pre-dedup emission).
    tolerated_sim_only_records = len(sim_records) - len(matched_sim_records)

    if tolerated_sim_only_records > 0:
        print("TRACE MATCH (sim has filtered pre-dedup records)")
        print(f"  tolerated sim-only records: {tolerated_sim_only_records}")
    else:
        print("TRACE MATCH")
    print(f"  records compared: {ui_record_idx}")
    return 0


def _read_filtered_trace_rows(path: str) -> list[tuple[str, str]]:
    # Include every structured byte line; trace labels are type names (e.g. "unsigned int") not a fixed set.
    return read_trace_rows(path, allowed_descriptions=None)


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


def compare_trace_files(sim_trace: str, ui_trace: str) -> int:
    sim_rows = _read_filtered_trace_rows(sim_trace)
    ui_rows = _read_filtered_trace_rows(ui_trace)

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


def run_trace_compare(db_file: str, ui_trace_file: str, sim_trace_file: str | None, cid: int | None) -> int:
    if not os.path.exists(db_file):
        raise RuntimeError(f"DB file does not exist: {db_file}")

    if sim_trace_file is not None:
        if not os.path.exists(sim_trace_file):
            raise RuntimeError(f"SIM trace does not exist: {sim_trace_file}")
        rc = emit_ui_trace_lockstep(db_file, ui_trace_file, sim_trace_file, cid)
        print(f"Wrote UI trace: {ui_trace_file}")
        return rc

    emit_ui_trace(db_file, ui_trace_file, cid)
    print(f"Wrote UI trace: {ui_trace_file}")
    return 0


def read_db_record_frames(db_file: str, max_records: int | None = None) -> list[DbRecordFrame]:
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    layouts_by_cid = load_layouts(conn, inspector)
    last_full_payload_bytes_by_cid: dict[int, int] = {}
    frames: list[DbRecordFrame] = []
    total_decompressed_blob_bytes = 0
    record_cap = max_records

    cursor = conn.cursor()
    cursor.execute(
        """
        SELECT Timestamps.Id, Timestamps.Timestamp, CollectionRecords.Records
        FROM CollectionRecords
        JOIN Timestamps ON Timestamps.Id = CollectionRecords.TimestampID
        ORDER BY Timestamps.Id ASC
        """
    )
    for timestamp_id, raw_time, compressed_blob in cursor.fetchall():
        timestamp = int(raw_time) if isinstance(raw_time, str) else int(raw_time)
        raw_blob = zlib.decompress(compressed_blob)
        total_decompressed_blob_bytes += len(raw_blob)
        blob_buf = ByteBuffer(raw_blob)

        while not blob_buf.Done():
            if record_cap is not None and len(frames) >= record_cap:
                conn.close()
                print("DB scan stats (partial)")
                print(f"  decompressed CollectionRecords payload bytes: {total_decompressed_blob_bytes}")
                return frames

            record_start_idx = blob_buf._read_idx
            cid = int(blob_buf.Read("H"))
            if cid not in layouts_by_cid:
                prev = frames[-1] if frames else None
                raise RuntimeError(
                    "FRAMING STOP: unknown CID (likely prior record consumed wrong byte count)\n"
                    f"  TimestampID: {timestamp_id}\n"
                    f"  Timestamp: {timestamp}\n"
                    f"  blob offset at cid read: {record_start_idx}\n"
                    f"  decompressed blob length: {len(raw_blob)}\n"
                    f"  read uint16 as cid: {cid}\n"
                    f"  previous record: {prev}\n"
                    f"  hex context: {hex_window(raw_blob, record_start_idx)}\n"
                    f"  hint: compare DataTypeNodes wire sizes to the producer; dump.py hits the same failure."
                )

            layout = layouts_by_cid[cid]
            payload_start_idx = blob_buf._read_idx
            action = decode_action(raw_blob, payload_start_idx)

            blob_buf.Read("B")
            trailing_chunks = consume_payload_and_count_tail(
                blob_buf, action, layout, last_full_payload_bytes_by_cid
            )
            if action == 4:
                last_full_payload_bytes_by_cid[cid] = sum(trailing_chunks)

            record_end_idx = blob_buf._read_idx
            frames.append(
                DbRecordFrame(
                    timestamp=timestamp,
                    cid=cid,
                    action=action,
                    total_after_cid=1 + sum(trailing_chunks),
                    raw_total_bytes=record_end_idx - record_start_idx,
                )
            )

        if blob_buf._read_idx != len(raw_blob):
            raise RuntimeError(
                "FRAMING STOP: trailing bytes after last record in blob\n"
                f"  TimestampID: {timestamp_id}\n"
                f"  Timestamp: {timestamp}\n"
                f"  read_idx: {blob_buf._read_idx}, blob_len: {len(raw_blob)}\n"
                f"  trailing byte count: {len(raw_blob) - blob_buf._read_idx}\n"
                f"  tail hex: {raw_blob[blob_buf._read_idx : blob_buf._read_idx + 48].hex()}"
            )

    conn.close()
    print("DB scan stats")
    print(f"  decompressed CollectionRecords payload bytes: {total_decompressed_blob_bytes}")
    return frames


def read_trace_record_totals(path: str) -> list[int]:
    rows = _read_filtered_trace_rows(path)

    totals: list[int] = []
    cur_total = 0
    cur_has_rows = False
    for row in rows:
        if row == ("2", "cid"):
            if cur_has_rows:
                totals.append(cur_total)
            cur_total = 0
            cur_has_rows = True
            continue
        cur_total += int(row[0])
    if cur_has_rows:
        totals.append(cur_total)
    return totals


def compare_first_divergence(
    db_frames: list[DbRecordFrame],
    sim_totals: list[int],
    allow_sim_only_records: bool,
) -> int:
    ui_totals = [f.total_after_cid for f in db_frames]

    if allow_sim_only_records:
        sim_counts = Counter(sim_totals)
        ui_counts = Counter(ui_totals)
        for total, ui_count in ui_counts.items():
            sim_count = sim_counts.get(total, 0)
            if ui_count > sim_count:
                print("FIRST DIVERGENCE")
                print(f"  missing record signature total-after-cid={total}")
                print(f"  ui count: {ui_count}")
                print(f"  sim count: {sim_count}")
                return 1

        tolerated = sum(sim_counts.values()) - sum(ui_counts.values())
        print("FRAME MATCH (sim-only records tolerated)")
        print(f"  db records: {len(ui_totals)}")
        print(f"  sim records: {len(sim_totals)}")
        print(f"  tolerated sim-only records: {tolerated}")
        return 0

    ui_idx = 0
    sim_idx = 0

    while ui_idx < len(ui_totals) and sim_idx < len(sim_totals):
        if ui_totals[ui_idx] == sim_totals[sim_idx]:
            ui_idx += 1
            sim_idx += 1
            continue

        frame = db_frames[ui_idx]
        print("FIRST DIVERGENCE")
        print(f"  record-index: {ui_idx}")
        print(f"  timestamp: {frame.timestamp}")
        print(f"  cid: {frame.cid}")
        print(f"  action: {frame.action}")
        print(f"  expected after-cid bytes (db replay): {ui_totals[ui_idx]}")
        print(f"  actual after-cid bytes (sim trace): {sim_totals[sim_idx]}")
        return 1

    if ui_idx < len(ui_totals):
        frame = db_frames[ui_idx]
        print("FIRST DIVERGENCE")
        print(f"  sim trace ended early at ui record-index {ui_idx}")
        print(f"  timestamp: {frame.timestamp}, cid: {frame.cid}, action: {frame.action}")
        print(f"  remaining ui records: {len(ui_totals) - ui_idx}")
        return 1

    if not allow_sim_only_records and sim_idx < len(sim_totals):
        print("FIRST DIVERGENCE")
        print(f"  ui replay ended early at sim record-index {sim_idx}")
        print(f"  remaining sim records: {len(sim_totals) - sim_idx}")
        return 1

    print("FRAME MATCH")
    print(f"  records matched: {len(ui_totals)}")
    return 0


def run_value_compare(
    db_file: str,
    sim_trace_file: str | None,
    allow_sim_only_records: bool,
    max_records: int | None,
) -> int:
    if not os.path.exists(db_file):
        raise RuntimeError(f"DB file does not exist: {db_file}")

    try:
        db_frames = read_db_record_frames(db_file, max_records=max_records)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    capped = max_records is not None and len(db_frames) >= max_records
    if capped:
        print("DB FRAMING OK (partial, --max-records cap hit)")
    else:
        print("DB FRAMING OK")
    print(f"  records parsed: {len(db_frames)}")

    if sim_trace_file is None:
        return 0
    if not os.path.exists(sim_trace_file):
        raise RuntimeError(f"SIM trace does not exist: {sim_trace_file}")

    sim_totals = read_trace_record_totals(sim_trace_file)
    return compare_first_divergence(
        db_frames,
        sim_totals,
        allow_sim_only_records=allow_sim_only_records,
    )
