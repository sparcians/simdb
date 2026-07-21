import os, sys, zlib
from typing import Any
from functools import partial

_PACKAGE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, _PACKAGE_ROOT)

from viewer.model.data_deserializers import ByteBuffer
from viewer.model.blob_handlers import *

# Tier 1: 0x00–0x0F — lifecycle / common (scalars + all collectables)
_CLOSED = 0x00
_FULL = 0x01
_CARRY = 0x02
# 0x03–0x0F reserved

# Tier 2: 0x10–0x1F — any-container (identical wire for contig & sparse)
_CONTAINER_SWAP = 0x10
_CONTAINER_MULTI_SWAP = 0x11
# 0x12–0x1F reserved

# Tier 3: 0x20–0x2F — contig-specific (FIFO queue semantics)
_CONTIG_ARRIVE = 0x20
_CONTIG_DEPART = 0x21
_CONTIG_BOOKENDS = 0x22
_CONTIG_MIMO = 0x23
# 0x24–0x2F reserved

# Tier 4: 0x30–0x3F — sparse-specific (explicit bin indices)
_SPARSE_REMOVE = 0x30
_SPARSE_ADD = 0x31
_SPARSE_MULTI_REMOVE = 0x32
# 0x33–0x3F reserved

_VALID_COMMON_ACTIONS = {
    _CLOSED,
    _FULL,
    _CARRY
}

_VALID_SCALAR_ACTIONS = _VALID_COMMON_ACTIONS

_VALID_ANY_CONTAINER_ACTIONS = {
    _CONTAINER_SWAP,
    _CONTAINER_MULTI_SWAP,
}

_VALID_CONTIG_CONTAINER_ACTIONS = _VALID_COMMON_ACTIONS | _VALID_ANY_CONTAINER_ACTIONS | \
    {
        _CONTIG_ARRIVE,
        _CONTIG_DEPART,
        _CONTIG_BOOKENDS,
        _CONTIG_MIMO,
    }

_VALID_SPARSE_CONTAINER_ACTIONS = _VALID_COMMON_ACTIONS | _VALID_ANY_CONTAINER_ACTIONS | \
    {
        _SPARSE_REMOVE,
        _SPARSE_ADD,
        _SPARSE_MULTI_REMOVE,
    }

class Resources:
    def __init__(self, dtype_inspector, simhier, handler):
        self.dtype_inspector = dtype_inspector
        self.simhier = simhier
        self.handler = handler
        self.buf = None

    def GetDeserializer(self, cid):
        type_name = self.dtype_inspector.GetDataTypeForCollectionID(cid)
        deserializer = self.dtype_inspector.GetDeserializer(type_name)
        if cid in self.simhier.GetContainerIDs():
            deserializer = deserializer._bin_deserializer

        return deserializer

class Context:
    def __init__(self):
        self.current_cid = None
        self.current_tick = None

def HandleCID(resources, context):
    if resources.buf.Done():
        return

    cid = resources.buf.Read('H')
    context.current_cid = cid
    return HandleAction

def HandleAction(resources, context):
    cid = context.current_cid
    action = int(resources.buf.Read('B'))

    if cid not in resources.simhier.GetContainerIDs():
        return partial(HandleScalarAction, action=action)
    elif resources.simhier.GetSparseFlagByCollectionID(cid):
        return partial(HandleSparseContainerAction, action=action)
    else:
        return partial(HandleContigContainerAction, action=action)

def HandleScalarAction(resources, context, action):
    assert action in _VALID_SCALAR_ACTIONS

    if action == _CLOSED:
        resources.handler.HandleScalarClosed(context)
    elif action == _CARRY:
        resources.handler.HandleScalarCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)
        deserialized = type_deserializer.Deserialize(resources.buf)

        if action == _FULL:
            resources.handler.HandleScalarFullDump(context, deserialized)

    return HandleCID

def HandleContigContainerAction(resources, context, action):
    assert action in _VALID_CONTIG_CONTAINER_ACTIONS

    if action == _CLOSED:
        resources.handler.HandleContigContainerClosed(context)
    elif action == _CARRY:
        resources.handler.HandleContigContainerCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)

        if action == _FULL:
            size = resources.buf.Read('H')
            deserialized = []
            for _ in range(size):
                deserialized.append(type_deserializer.Deserialize(resources.buf))

            resources.handler.HandleContigContainerFullDump(context, deserialized)

        elif action == _CONTAINER_SWAP:
            bin_idx = resources.buf.Read('H')
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerSwap(context, bin_idx, deserialized)

        elif action == _CONTAINER_MULTI_SWAP:
            count = resources.buf.Read('B')
            for _ in range(count):
                bin_idx = resources.buf.Read('H')
                deserialized = type_deserializer.Deserialize(resources.buf)
                resources.handler.HandleContigContainerSwap(context, bin_idx, deserialized)

        elif action == _CONTIG_ARRIVE:
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerArrival(context, deserialized)

        elif action == _CONTIG_DEPART:
            resources.handler.HandleContigContainerDeparture(context)

        elif action == _CONTIG_BOOKENDS:
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerBookends(context, deserialized)

        elif action == _CONTIG_MIMO:
            depart_count = resources.buf.Read('B')
            arrive_count = resources.buf.Read('B')
            for _ in range(depart_count):
                resources.handler.HandleContigContainerDeparture(context)
            for _ in range(arrive_count):
                deserialized = type_deserializer.Deserialize(resources.buf)
                resources.handler.HandleContigContainerArrival(context, deserialized)

    return HandleCID

def HandleSparseContainerAction(resources, context, action):
    assert action in _VALID_SPARSE_CONTAINER_ACTIONS

    if action == _CLOSED:
        resources.handler.HandleSparseContainerClosed(context)
    elif action == _CARRY:
        resources.handler.HandleSparseContainerCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)

        if action == _FULL:
            size = resources.buf.Read('H')
            deserialized = {}
            for _ in range(size):
                bin_idx = resources.buf.Read('H')
                bin_deserialized = type_deserializer.Deserialize(resources.buf)
                deserialized[bin_idx] = bin_deserialized

            resources.handler.HandleSparseContainerFullDump(context, deserialized)

        elif action == _CONTAINER_SWAP:
            bin_idx = resources.buf.Read('H')
            bin_deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleSparseContainerExchangedBin(context, bin_idx, bin_deserialized)

        elif action == _CONTAINER_MULTI_SWAP:
            count = resources.buf.Read('B')
            for _ in range(count):
                bin_idx = resources.buf.Read('H')
                bin_deserialized = type_deserializer.Deserialize(resources.buf)
                resources.handler.HandleSparseContainerExchangedBin(context, bin_idx, bin_deserialized)

        elif action == _SPARSE_REMOVE:
            bin_idx = resources.buf.Read('H')
            resources.handler.HandleSparseContainerRemovedBin(context, bin_idx)

        elif action == _SPARSE_ADD:
            bin_idx = resources.buf.Read('H')
            bin_deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleSparseContainerAddedBin(context, bin_idx, bin_deserialized)

        elif action == _SPARSE_MULTI_REMOVE:
            count = resources.buf.Read('B')
            for _ in range(count):
                bin_idx = resources.buf.Read('H')
                resources.handler.HandleSparseContainerRemovedBin(context, bin_idx)

    return HandleCID

class BlobIterator:
    def __init__(self, dtype_inspector, simhier):
        self._dtype_inspector = dtype_inspector
        self._simhier = simhier
        self._final_tick = None

    @property
    def connection(self):
        return self._dtype_inspector.connection

    def GetFinalTick(self):
        assert self._final_tick is not None, 'Iterate() never called'
        return self._final_tick

    def Iterate(self, handler: BlobHandler, time_range: Any = None, lookback: bool = False,
                clock_ids: Any = None):
        cursor = self.connection.cursor()
        if time_range is None:
            rows = self._fetch_all_rows(cursor, clock_ids)
        else:
            if isinstance(time_range, int):
                time_range = [time_range, time_range]
            elif not isinstance(time_range, list):
                time_range = list(time_range)
                if len(time_range) == 1:
                    time_range *= 2

            lo, hi = int(time_range[0]), int(time_range[1])
            if lookback:
                cursor.execute('SELECT Heartbeat FROM CollectionGlobals')
                heartbeat = int(cursor.fetchone()[0])
                rows = self._fetch_lookback_rows(cursor, lo, hi, heartbeat, clock_ids)
            else:
                rows = self._fetch_time_range_rows(cursor, lo, hi, clock_ids)

        resources = Resources(self._dtype_inspector, self._simhier, handler)
        context = Context()
        for tsid, ts, compressed_blob in rows:
            resources.buf = ByteBuffer(zlib.decompress(compressed_blob))
            context.current_tick = int(ts)

            handler_func = HandleCID
            while handler_func:
                handler_func = handler_func(resources, context)
            handler.SnapshotTick(context)

        self._final_tick = context.current_tick

    def _has_timestamp_clocks(self, cursor):
        cursor.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='TimestampClocks'"
        )
        return cursor.fetchone() is not None

    def _normalize_clock_ids(self, clock_ids):
        if clock_ids is None:
            return None
        if isinstance(clock_ids, int):
            return [clock_ids]
        return list(clock_ids)

    def _use_clock_filter(self, cursor, clock_ids):
        clock_ids = self._normalize_clock_ids(clock_ids)
        return bool(clock_ids) and self._has_timestamp_clocks(cursor)

    def _fetch_all_rows(self, cursor, clock_ids=None):
        clock_ids = self._normalize_clock_ids(clock_ids)
        if self._use_clock_filter(cursor, clock_ids):
            placeholders = ','.join('?' for _ in clock_ids)
            cursor.execute(
                'SELECT DISTINCT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'INNER JOIN TimestampClocks tc ON tc.TimestampID = t.Id '
                f'WHERE tc.ClockID IN ({placeholders}) '
                'ORDER BY t.Id ASC',
                tuple(clock_ids),
            )
        else:
            cursor.execute(
                'SELECT DISTINCT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'ORDER BY t.Id ASC'
            )
        return cursor.fetchall()

    def _fetch_time_range_rows(self, cursor, lo, hi, clock_ids=None):
        clock_ids = self._normalize_clock_ids(clock_ids)
        if self._use_clock_filter(cursor, clock_ids):
            placeholders = ','.join('?' for _ in clock_ids)
            cursor.execute(
                'SELECT DISTINCT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'INNER JOIN TimestampClocks tc ON tc.TimestampID = t.Id '
                f'WHERE tc.ClockID IN ({placeholders}) '
                'AND CAST(t.Timestamp AS INTEGER) >= ? AND CAST(t.Timestamp AS INTEGER) <= ? '
                'ORDER BY t.Id ASC',
                tuple(clock_ids) + (lo, hi),
            )
        else:
            cursor.execute(
                'SELECT DISTINCT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'WHERE CAST(t.Timestamp AS INTEGER) >= ? AND CAST(t.Timestamp AS INTEGER) <= ? '
                'ORDER BY t.Id ASC',
                (lo, hi),
            )
        return cursor.fetchall()

    def _fetch_lookback_rows(self, cursor, start_tick, end_tick, heartbeat, clock_ids=None):
        clock_ids = self._normalize_clock_ids(clock_ids)
        if self._use_clock_filter(cursor, clock_ids):
            placeholders = ','.join('?' for _ in clock_ids)
            params = tuple(clock_ids) + (start_tick, heartbeat) + tuple(clock_ids) + (end_tick,)
            cursor.execute(
                'WITH eligible AS ('
                '  SELECT DISTINCT t.Id '
                '  FROM Timestamps t '
                '  INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                '  INNER JOIN TimestampClocks tc ON tc.TimestampID = t.Id '
                f'  WHERE tc.ClockID IN ({placeholders}) '
                '    AND CAST(t.Timestamp AS INTEGER) <= ?'
                '), warmup AS ('
                '  SELECT Id FROM eligible ORDER BY Id DESC LIMIT ?'
                ') '
                'SELECT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'INNER JOIN TimestampClocks tc ON tc.TimestampID = t.Id '
                f'WHERE tc.ClockID IN ({placeholders}) '
                '  AND t.Id >= (SELECT MIN(Id) FROM warmup) '
                '  AND CAST(t.Timestamp AS INTEGER) <= ? '
                'ORDER BY t.Id ASC',
                params,
            )
        else:
            cursor.execute(
                'WITH warmup AS ('
                '  SELECT t.Id '
                '  FROM Timestamps t '
                '  INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                '  WHERE CAST(t.Timestamp AS INTEGER) <= ? '
                '  ORDER BY t.Id DESC '
                '  LIMIT ?'
                ') '
                'SELECT t.Id, t.Timestamp, cr.Records '
                'FROM Timestamps t '
                'INNER JOIN CollectionRecords cr ON cr.TimestampID = t.Id '
                'WHERE t.Id >= (SELECT MIN(Id) FROM warmup) '
                '  AND CAST(t.Timestamp AS INTEGER) <= ? '
                'ORDER BY t.Id ASC',
                (start_tick, heartbeat, end_tick),
            )
        return cursor.fetchall()

def main():
    from viewer.model.dtype_inspector import DataTypeInspector
    from viewer.model.simhier import SimHierarchy
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--tick", type=int, help="Extract the data values at this tick")
    parser.add_argument("database", help="Path to the database file")
    args = parser.parse_args()

    dtype_inspector = DataTypeInspector(args.database)
    simhier = SimHierarchy(dtype_inspector.connection, dtype_inspector)
    iterator = BlobIterator(dtype_inspector, simhier)

    # No tick provided? Use smoke test handler.
    if args.tick is None:
        handler = SmokeTestHandler()
        iterator.Iterate(handler)

    # Tick provided? Use the last <heartbeat> collection records at or before tick.
    else:
        handler = DataExtractionHandler(simhier)
        iterator.Iterate(handler, args.tick, lookback=True)
        print(handler.GetAllFinalValues())

if __name__ == '__main__':
    main()
