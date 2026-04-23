# -*- coding: utf-8 -*-
"""
Per-collectable replay of Argos collection-record payloads (CID prefix is stripped by caller).

Mirrors C++ minifier layouts in include/simdb/apps/argos/Minifiers.hpp. Used by dump.py tests;
not wired into the viewer UI.
"""
from __future__ import annotations

import zlib
from typing import Any, Dict, List, Optional, Tuple

from viewer.model.data_deserializers import ByteBuffer


def _split_container_type_name(type_name: str) -> Optional[Tuple[str, int, bool]]:
    for which, sparse in (("_sparse_capacity", True), ("_contig_capacity", False)):
        key = which
        idx = type_name.find(key)
        if idx != -1:
            base = type_name[:idx]
            cap = int(type_name[idx + len(key) :])
            return base, cap, sparse
    return None


class CollectableReplayerBase:
    """Knows one CID and how to consume the next payload from a shared ByteBuffer."""

    def __init__(self, cid: int, type_name: str, inspector: Any) -> None:
        self.cid = int(cid)
        self.type_name = str(type_name)
        self._inspector = inspector
        self._latest_value: Any = {}
        self._latest_time_point: Optional[int] = None
        self._session: Optional["CollectionReplaySession"] = None

    def replay_next(self, buf: ByteBuffer) -> Any:
        raise NotImplementedError

    def GetDataValueAtTime(self, time_point: int) -> Any:
        if self._session is None:
            return {}
        return self._session.GetDataValueAtTime(self.cid, int(time_point))

    def ResetReplayState(self) -> None:
        self._latest_value = {}
        self._latest_time_point = None

    def _SetSession(self, session: "CollectionReplaySession") -> None:
        self._session = session

    def _ObserveReplayValue(self, time_point: int, value: Any) -> None:
        self._latest_time_point = int(time_point)
        self._latest_value = value

    def _GetLatestReplayValue(self) -> Any:
        return self._latest_value


class ScalarRawReplayer(CollectableReplayerBase):
    """POD, enum, string: payload is exactly one scalar (no minifier prefix)."""

    def __init__(self, cid: int, type_name: str, inspector: Any) -> None:
        super().__init__(cid, type_name, inspector)
        self._deserializer = inspector.GetDeserializer(type_name)

    def replay_next(self, buf: ByteBuffer) -> Any:
        return self._deserializer.Deserialize(buf)


class StructMinifiedReplayer(CollectableReplayerBase):
    """
    Struct with ArgosCollector: payload is [uint16 action][body...].
    FULL (0): body is fixed-width struct bytes (see StructDeserializer.GetNumBytes()).
    CARRY (1): no body; value unchanged from last FULL/CARRY resolution.
    """

    _FULL = 0
    _CARRY = 1

    def __init__(self, cid: int, type_name: str, inspector: Any) -> None:
        super().__init__(cid, type_name, inspector)
        self._deserializer = inspector.GetDeserializer(type_name)
        self._last: Any = None

    def ResetReplayState(self) -> None:
        super().ResetReplayState()
        self._last = None

    def replay_next(self, buf: ByteBuffer) -> Any:
        action = int(buf.Read("H"))
        if action == self._FULL:
            nbytes = self._deserializer.GetNumBytes()
            raw = buf.Extract(nbytes)
            self._last = self._deserializer.Deserialize(raw)
            return self._last
        if action == self._CARRY:
            if self._last is None:
                # With windowed replay, the anchor FULL may be just outside the
                # replay window. Keep this non-fatal for now and treat as empty.
                return {}
            return self._last
        raise RuntimeError(
            f"CID {self.cid}: unknown struct MinifierAction {action} for {self.type_name!r}"
        )


class ContigContainerMinifiedReplayer(CollectableReplayerBase):
    """Contiguous container: minifier actions match C++ enum order (0..5)."""

    _FULL = 0
    _CARRY = 1
    _SWAP = 2
    _ARRIVE = 3
    _DEPART = 4
    _BOOKENDS = 5

    def __init__(self, cid: int, type_name: str, inspector: Any, base: str, capacity: int) -> None:
        super().__init__(cid, type_name, inspector)
        self._capacity = capacity
        self._bin = inspector.GetDeserializer(base)
        self._items: List[Any] = []

    def ResetReplayState(self) -> None:
        super().ResetReplayState()
        self._items = []

    def replay_next(self, buf: ByteBuffer) -> Any:
        action = int(buf.Read("H"))
        if action == self._FULL:
            size = int(buf.Read("H"))
            if size > self._capacity:
                raise RuntimeError(f"CID {self.cid}: contig size {size} > capacity {self._capacity}")
            self._items = [self._bin.Deserialize(buf) for _ in range(size)]
            return list(self._items)
        if action == self._CARRY:
            return list(self._items)
        if action == self._SWAP:
            idx = int(buf.Read("H"))
            new_value = self._bin.Deserialize(buf)
            if idx < len(self._items):
                self._items[idx] = new_value
            elif idx < self._capacity:
                # Windowed replay can start mid-stream; fill unknown slots.
                self._items.extend([{}] * (idx - len(self._items)))
                self._items.append(new_value)
            return list(self._items)
        if action == self._ARRIVE:
            if len(self._items) < self._capacity:
                self._items.append(self._bin.Deserialize(buf))
            else:
                # Keep parser in sync, but do not grow past declared capacity.
                _ = self._bin.Deserialize(buf)
            return list(self._items)
        if action == self._DEPART:
            if not self._items:
                return list(self._items)
            self._items.pop(0)
            return list(self._items)
        if action == self._BOOKENDS:
            new_value = self._bin.Deserialize(buf)
            if not self._items:
                if self._capacity > 0:
                    self._items.append(new_value)
                return list(self._items)
            self._items.pop(0)
            self._items.append(new_value)
            return list(self._items)
        raise RuntimeError(f"CID {self.cid}: unknown contig MinifierAction {action}")


class SparseContainerMinifiedReplayer(CollectableReplayerBase):
    _FULL = 0
    _CARRY = 1
    _EXCHANGE = 2
    _REMOVE = 3

    def __init__(self, cid: int, type_name: str, inspector: Any, base: str, capacity: int) -> None:
        super().__init__(cid, type_name, inspector)
        self._capacity = capacity
        self._bin = inspector.GetDeserializer(base)
        self._cells: List[Optional[Any]] = [None] * capacity

    def ResetReplayState(self) -> None:
        super().ResetReplayState()
        self._cells = [None] * self._capacity

    def replay_next(self, buf: ByteBuffer) -> Any:
        action = int(buf.Read("H"))
        if action == self._FULL:
            n = int(buf.Read("H"))
            self._cells = [None] * self._capacity
            for _ in range(n):
                idx = int(buf.Read("H"))
                if idx < self._capacity:
                    self._cells[idx] = self._bin.Deserialize(buf)
                else:
                    _ = self._bin.Deserialize(buf)
            return list(self._cells)
        if action == self._CARRY:
            return list(self._cells)
        if action == self._EXCHANGE:
            idx = int(buf.Read("H"))
            new_value = self._bin.Deserialize(buf)
            if idx < self._capacity:
                self._cells[idx] = new_value
            return list(self._cells)
        if action == self._REMOVE:
            idx = int(buf.Read("H"))
            if idx < self._capacity:
                self._cells[idx] = None
            return list(self._cells)
        raise RuntimeError(f"CID {self.cid}: unknown sparse MinifierAction {action}")


def CreateCollectableReplayer(cid: int, type_name: str, inspector: Any) -> CollectableReplayerBase:
    meta = _split_container_type_name(type_name)
    if meta is not None:
        base, capacity, sparse = meta
        if sparse:
            return SparseContainerMinifiedReplayer(cid, type_name, inspector, base, capacity)
        return ContigContainerMinifiedReplayer(cid, type_name, inspector, base, capacity)

    if inspector.GetStructDefn(type_name) is not None:
        return StructMinifiedReplayer(cid, type_name, inspector)

    return ScalarRawReplayer(cid, type_name, inspector)


class CollectionReplaySession:
    """
    Rebuild collectable values for a requested time point by replaying only that
    time point's heartbeat window from the DB.
    """

    def __init__(self, db_conn: Any, replayers_by_cid: Dict[int, CollectableReplayerBase]) -> None:
        self._conn = db_conn
        self._cursor = db_conn.cursor()
        self._replayers_by_cid = replayers_by_cid
        self._last_replayed_time_point: Optional[int] = None

        self._cursor.execute("SELECT Heartbeat FROM CollectionGlobals")
        self._heartbeat = int(self._cursor.fetchone()[0])

        self._cursor.execute("SELECT Id,Timestamp FROM Timestamps ORDER BY Id ASC")
        self._timestamps: List[Tuple[int, int]] = []
        for timestamp_id, time_point in self._cursor.fetchall():
            if isinstance(time_point, str):
                time_point = int(time_point)
            self._timestamps.append((int(timestamp_id), int(time_point)))

        for replayer in self._replayers_by_cid.values():
            replayer._SetSession(self)

    def GetDataValueAtTime(self, cid: int, time_point: int) -> Any:
        time_point = int(time_point)
        if self._last_replayed_time_point != time_point:
            self._ReplayWindowForTimePoint(time_point)
            self._last_replayed_time_point = time_point

        replayer = self._replayers_by_cid[cid]
        return replayer._GetLatestReplayValue()

    def _ReplayWindowForTimePoint(self, time_point: int) -> None:
        window = [(timestamp_id, ts) for timestamp_id, ts in self._timestamps if ts <= time_point]
        if not window:
            for replayer in self._replayers_by_cid.values():
                replayer.ResetReplayState()
            return

        window = window[-self._heartbeat :]
        wanted_timestamp_ids = {timestamp_id for timestamp_id, _ in window}
        time_points_by_timestamp_id = {timestamp_id: ts for timestamp_id, ts in window}

        for replayer in self._replayers_by_cid.values():
            replayer.ResetReplayState()

        placeholders = ",".join("?" for _ in wanted_timestamp_ids)
        cmd = (
            "SELECT TimestampID,Records FROM CollectionRecords "
            f"WHERE TimestampID IN ({placeholders}) ORDER BY TimestampID ASC"
        )
        self._cursor.execute(cmd, tuple(sorted(wanted_timestamp_ids)))

        for timestamp_id, compressed_blob in self._cursor.fetchall():
            timestamp_id = int(timestamp_id)
            time_point_at_blob = time_points_by_timestamp_id[timestamp_id]
            buf = ByteBuffer(zlib.decompress(compressed_blob))

            while not buf.Done():
                blob_cid = int(buf.Read("H"))
                replayer = self._replayers_by_cid[blob_cid]
                value = replayer.replay_next(buf)
                replayer._ObserveReplayValue(time_point_at_blob, value)
