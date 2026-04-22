# -*- coding: utf-8 -*-
"""
Per-collectable replay of Argos collection-record payloads (CID prefix is stripped by caller).

Mirrors C++ minifier layouts in include/simdb/apps/argos/Minifiers.hpp. Used by dump.py tests;
not wired into the viewer UI.
"""
from __future__ import annotations

from typing import Any, List, Optional, Tuple

from viewer.model.data_deserializers import ByteBuffer, CreateDeserializer


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

    def replay_next(self, buf: ByteBuffer) -> Any:
        raise NotImplementedError


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

    def replay_next(self, buf: ByteBuffer) -> Any:
        action = int(buf.Read("H"))
        if action == self._FULL:
            nbytes = self._deserializer.GetNumBytes()
            raw = buf.Extract(nbytes)
            self._last = self._deserializer.Deserialize(raw)
            return self._last
        if action == self._CARRY:
            if self._last is None:
                raise RuntimeError(
                    f"CID {self.cid}: CARRY before any FULL for struct {self.type_name!r}"
                )
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
            self._items[idx] = self._bin.Deserialize(buf)
            return list(self._items)
        if action == self._ARRIVE:
            self._items.append(self._bin.Deserialize(buf))
            return list(self._items)
        if action == self._DEPART:
            if not self._items:
                raise RuntimeError(f"CID {self.cid}: DEPART on empty container")
            self._items.pop(0)
            return list(self._items)
        if action == self._BOOKENDS:
            if not self._items:
                raise RuntimeError(f"CID {self.cid}: BOOKENDS on empty container")
            self._items.pop(0)
            self._items.append(self._bin.Deserialize(buf))
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

    def replay_next(self, buf: ByteBuffer) -> Any:
        action = int(buf.Read("H"))
        if action == self._FULL:
            n = int(buf.Read("H"))
            self._cells = [None] * self._capacity
            for _ in range(n):
                idx = int(buf.Read("H"))
                self._cells[idx] = self._bin.Deserialize(buf)
            return list(self._cells)
        if action == self._CARRY:
            return list(self._cells)
        if action == self._EXCHANGE:
            idx = int(buf.Read("H"))
            self._cells[idx] = self._bin.Deserialize(buf)
            return list(self._cells)
        if action == self._REMOVE:
            idx = int(buf.Read("H"))
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
