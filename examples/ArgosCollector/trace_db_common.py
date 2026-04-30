from dataclasses import dataclass
import sqlite3
import struct
import zlib

from viewer.model.data_deserializers import ByteBuffer
from viewer.model.dtype_inspector import DataTypeInspector


@dataclass
class CidLayout:
    cid: int
    type_name: str
    mode: str  # scalar | contig | sparse
    value_num_bytes: int


@dataclass
class DbRecordFrame:
    timestamp: int
    cid: int
    action: int
    total_after_cid: int  # action (1) + payload bytes
    raw_total_bytes: int  # cid (2) + action + payload


def decode_action(raw_blob: bytes, payload_start_idx: int) -> int:
    if payload_start_idx >= len(raw_blob):
        raise RuntimeError("Malformed record: missing action byte")
    return struct.unpack("B", raw_blob[payload_start_idx : payload_start_idx + 1])[0]


def split_container_type_name(type_name: str):
    for which, sparse in (("_sparse_capacity", True), ("_contig_capacity", False)):
        idx = type_name.find(which)
        if idx != -1:
            base = type_name[:idx]
            cap = int(type_name[idx + len(which) :])
            return base, cap, sparse
    return None


def load_layouts(conn: sqlite3.Connection, inspector: DataTypeInspector) -> dict[int, CidLayout]:
    cursor = conn.cursor()
    cursor.execute("SELECT TypeName,SerializationCID FROM CollectableTreeNodes")

    layouts: dict[int, CidLayout] = {}
    for type_name, cid in cursor.fetchall():
        cid = int(cid)
        type_name = str(type_name)
        meta = split_container_type_name(type_name)
        if meta is None:
            des = inspector.GetDeserializer(type_name)
            layouts[cid] = CidLayout(cid=cid, type_name=type_name, mode="scalar", value_num_bytes=des.GetNumBytes())
        else:
            base_type, _capacity, sparse = meta
            bin_des = inspector.GetDeserializer(base_type)
            layouts[cid] = CidLayout(
                cid=cid,
                type_name=type_name,
                mode="sparse" if sparse else "contig",
                value_num_bytes=bin_des.GetNumBytes(),
            )
    return layouts


def consume_payload_and_count_tail(
    buf: ByteBuffer,
    action: int,
    layout: CidLayout,
    last_full_payload_bytes_by_cid: dict[int, int],
) -> list[int]:
    if action in (0, 2):  # DISABLED / QUIETED
        return []
    if action in (1, 3):  # ENABLED / AWAKENED
        full_payload_bytes = last_full_payload_bytes_by_cid.get(layout.cid, 0)
        if full_payload_bytes > 0:
            buf.Extract(full_payload_bytes)
            return [full_payload_bytes]
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


def hex_window(blob: bytes, center_idx: int, radius: int = 32) -> str:
    lo = max(0, center_idx - radius)
    hi = min(len(blob), center_idx + radius)
    return blob[lo:hi].hex()

