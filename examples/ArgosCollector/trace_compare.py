import argparse
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


@dataclass
class _CidLayout:
    cid: int
    type_name: str
    mode: str  # scalar | contig | sparse
    value_num_bytes: int


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
    last_payload_tail_by_cid: dict[int, int],
) -> list[int]:
    # We already consumed 1 action byte. Return appended chunk sizes after action.
    if action in (0, 2):  # DISABLED / QUIETED
        return []

    if action in (1, 3):  # ENABLED / AWAKENED (optional replay tail)
        # Producer appends prior bytes from offset sizeof(cid), i.e. previous
        # [action + payload_tail] for this CID.
        prior_tail = last_payload_tail_by_cid.get(layout.cid, 0)
        replay_tail = 0 if prior_tail == 0 else (1 + prior_tail)
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


def _emit_ui_trace(db_file: str, out_path: str, selected_cid: int | None) -> None:
    inspector = DataTypeInspector(db_file)
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    layouts_by_cid = _load_layouts(conn, inspector)
    last_payload_tail_by_cid: dict[int, int] = {}

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
                action = _decode_action(raw_blob, payload_start_idx)

                blob_buf.Read("B")
                trailing_chunks = _consume_payload_and_count_tail(
                    blob_buf, action, layout, last_payload_tail_by_cid
                )

                if action >= 4:
                    last_payload_tail_by_cid[cid] = sum(trailing_chunks)

                if selected_cid is not None and cid != selected_cid:
                    continue

                out.write("2\tcid\n")
                out.write("1\taction\n")
                for nbytes in trailing_chunks:
                    out.write(f"{nbytes}\tbytes\n")


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

    def _is_sim_only_pair(rows: list[tuple[str, str]], i: int) -> bool:
        return i + 1 < len(rows) and rows[i] == ("2", "cid") and rows[i + 1] == ("1", "action")

    i = 0  # sim index
    j = 0  # ui index
    tolerated = 0
    while i < len(sim_rows) and j < len(ui_rows):
        if sim_rows[i] == ui_rows[j]:
            i += 1
            j += 1
            continue

        # C++ trace is emitted pre-dedup; DB/UI replay is post-dedup.
        # Skip any sim-only cid/action pairs that don't survive to DB rows.
        if _is_sim_only_pair(sim_rows, i):
            i += 2
            tolerated += 2
            continue

        print("TRACE DIVERGENCE")
        print(f"  sim row: {i + 2}")
        print(f"  ui  row: {j + 2}")
        print(f"  sim: {sim_rows[i][0]!r}\t{sim_rows[i][1]!r}")
        print(f"  ui : {ui_rows[j][0]!r}\t{ui_rows[j][1]!r}")
        return 1

    while i < len(sim_rows) and _is_sim_only_pair(sim_rows, i):
        i += 2
        tolerated += 2

    if i != len(sim_rows) or j != len(ui_rows):
        print("TRACE LENGTH MISMATCH")
        print(f"  sim rows: {len(sim_rows)} (consumed {i})")
        print(f"  ui  rows: {len(ui_rows)} (consumed {j})")
        return 1

    if tolerated > 0:
        print("TRACE MATCH (sim has filtered cid/action-only rows)")
        print(f"  rows compared: {len(ui_rows)}")
        print(f"  tolerated sim-only rows: {tolerated}")
    else:
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
