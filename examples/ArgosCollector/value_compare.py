import argparse
from collections import Counter
import os
import sqlite3
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

# Repo root is two levels above this directory.
_REPO_ROOT = Path(__file__).resolve().parents[2]
_ARGOS_PKG = _REPO_ROOT / "python" / "argos"
if str(_ARGOS_PKG) not in sys.path:
    sys.path.insert(0, str(_ARGOS_PKG))

from viewer.model.data_deserializers import ByteBuffer
from viewer.model.dtype_inspector import DataTypeInspector

from trace_format import read_trace_rows


@dataclass
class _CidLayout:
    cid: int
    type_name: str
    mode: str  # scalar | contig | sparse
    value_num_bytes: int


@dataclass
class _DbRecordFrame:
    timestamp: int
    cid: int
    action: int
    total_after_cid: int  # action (1) + payload bytes
    raw_total_bytes: int  # cid (2) + action + payload


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


def _decode_action(raw_blob: bytes, payload_start_idx: int) -> int:
    if payload_start_idx >= len(raw_blob):
        raise RuntimeError("Malformed record: missing action byte")
    return struct.unpack("B", raw_blob[payload_start_idx : payload_start_idx + 1])[0]


def _split_container_type_name(type_name: str):
    for which, sparse in (("_sparse_capacity", True), ("_contig_capacity", False)):
        idx = type_name.find(which)
        if idx != -1:
            base = type_name[:idx]
            cap = int(type_name[idx + len(which) :])
            return base, cap, sparse
    return None


def _load_layouts(conn: sqlite3.Connection, inspector: DataTypeInspector) -> dict[int, _CidLayout]:
    cursor = conn.cursor()
    cursor.execute("SELECT TypeName,SerializationCID FROM CollectableTreeNodes")

    layouts: dict[int, _CidLayout] = {}
    for type_name, cid in cursor.fetchall():
        cid = int(cid)
        type_name = str(type_name)
        meta = _split_container_type_name(type_name)
        if meta is None:
            des = inspector.GetDeserializer(type_name)
            layouts[cid] = _CidLayout(cid=cid, type_name=type_name, mode="scalar", value_num_bytes=des.GetNumBytes())
        else:
            base_type, _capacity, sparse = meta
            bin_des = inspector.GetDeserializer(base_type)
            layouts[cid] = _CidLayout(
                cid=cid,
                type_name=type_name,
                mode="sparse" if sparse else "contig",
                value_num_bytes=bin_des.GetNumBytes(),
            )
    return layouts


def _consume_payload_and_count_tail(
    buf: ByteBuffer,
    action: int,
    layout: _CidLayout,
    last_sent_after_cid_by_cid: dict[int, int],
) -> list[int]:
    if action in (0, 2):  # DISABLED / QUIETED
        return []
    if action in (1, 3):  # ENABLED / AWAKENED
        replay_tail = last_sent_after_cid_by_cid.get(layout.cid, 0)
        if replay_tail > 0:
            buf.Extract(replay_tail)
            return [replay_tail]
        return []

    if layout.mode == "scalar":
        if action == 4:  # FULL
            buf.Extract(layout.value_num_bytes)
            return [layout.value_num_bytes]
        if action == 5:  # CARRY
            return []
        raise RuntimeError(f"CID {layout.cid}: unknown scalar action {action}")

    if layout.mode == "contig":
        bin_n = layout.value_num_bytes
        if action == 4:  # FULL
            size = int(buf.Read("H"))
            buf.Extract(size * bin_n)
            return [2] + [bin_n] * size
        if action == 5:  # CARRY
            return []
        if action == 6:  # SWAP
            buf.Read("H")
            buf.Extract(bin_n)
            return [2, bin_n]
        if action in (7, 9):  # ARRIVE / BOOKENDS
            buf.Extract(bin_n)
            return [bin_n]
        if action == 8:  # DEPART
            return []
        raise RuntimeError(f"CID {layout.cid}: unknown contig action {action}")

    if layout.mode == "sparse":
        bin_n = layout.value_num_bytes
        if action == 4:  # FULL
            size = int(buf.Read("H"))
            for _ in range(size):
                buf.Read("H")
                buf.Extract(bin_n)
            chunks = [2]
            for _ in range(size):
                chunks.extend([2, bin_n])
            return chunks
        if action == 5:  # CARRY
            return []
        if action == 6:  # EXCHANGE
            buf.Read("H")
            buf.Extract(bin_n)
            return [2, bin_n]
        if action == 7:  # REMOVE
            buf.Read("H")
            return [2]
        raise RuntimeError(f"CID {layout.cid}: unknown sparse action {action}")

    raise RuntimeError(f"CID {layout.cid}: unknown layout mode {layout.mode!r}")


def _hex_window(blob: bytes, center_idx: int, radius: int = 32) -> str:
    lo = max(0, center_idx - radius)
    hi = min(len(blob), center_idx + radius)
    return blob[lo:hi].hex()


def _read_db_record_frames(db_file: str, max_records: int | None = None) -> list[_DbRecordFrame]:
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    layouts_by_cid = _load_layouts(conn, inspector)
    last_sent_after_cid_by_cid: dict[int, int] = {}
    frames: list[_DbRecordFrame] = []
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
                    f"  hex context: {_hex_window(raw_blob, record_start_idx)}\n"
                    f"  hint: compare DataTypeNodes wire sizes to the producer; dump.py hits the same failure."
                )

            layout = layouts_by_cid[cid]
            payload_start_idx = blob_buf._read_idx
            action = _decode_action(raw_blob, payload_start_idx)

            blob_buf.Read("B")
            trailing_chunks = _consume_payload_and_count_tail(
                blob_buf, action, layout, last_sent_after_cid_by_cid
            )
            if action >= 4:
                last_sent_after_cid_by_cid[cid] = 1 + sum(trailing_chunks)

            record_end_idx = blob_buf._read_idx
            frames.append(
                _DbRecordFrame(
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


def _read_trace_record_totals(path: str) -> list[int]:
    rows = read_trace_rows(
        path,
        allowed_descriptions={"cid", "action", "bytes", "heartbeat replay bytes", "lifecycle payload tail"},
    )

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


def _compare_first_divergence(
    db_frames: list[_DbRecordFrame],
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


def main() -> int:
    args = _parse_args()
    if not os.path.exists(args.db_file):
        raise RuntimeError(f"DB file does not exist: {args.db_file}")

    try:
        db_frames = _read_db_record_frames(args.db_file, max_records=args.max_records)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    capped = args.max_records is not None and len(db_frames) >= args.max_records
    if capped:
        print("DB FRAMING OK (partial, --max-records cap hit)")
    else:
        print("DB FRAMING OK")
    print(f"  records parsed: {len(db_frames)}")

    if args.sim_trace_file is None:
        return 0
    if not os.path.exists(args.sim_trace_file):
        raise RuntimeError(f"SIM trace does not exist: {args.sim_trace_file}")

    sim_totals = _read_trace_record_totals(args.sim_trace_file)
    return _compare_first_divergence(
        db_frames,
        sim_totals,
        allow_sim_only_records=args.allow_sim_only_records,
    )


if __name__ == "__main__":
    raise SystemExit(main())
