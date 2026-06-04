import os, sys, zlib
from typing import Any
from functools import partial
from collections import OrderedDict

_PACKAGE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, _PACKAGE_ROOT)

from viewer.model.data_deserializers import ByteBuffer
from viewer.model.blob_handlers import *

# Common actions applicable to all
_DISABLED = 0
_ENABLED = 1
_QUIETED = 2
_AWAKENED = 3
_FULL = 4
_CARRY = 5

# Contig container-specific actions
_CONTIG_CONTAINER_SWAP = 6
_CONTIG_CONTAINER_ARRIVE = 7
_CONTIG_CONTAINER_DEPART = 8
_CONTIG_CONTAINER_BOOKENDS = 9

# Sparse container-specific actions
_SPARSE_CONTAINER_EXCHANGE = 6
_SPARSE_CONTAINER_REMOVE = 7

_VALID_COMMON_ACTIONS = {
    _DISABLED,
    _ENABLED,
    _QUIETED,
    _AWAKENED,
    _FULL,
    _CARRY
}

_VALID_SCALAR_ACTIONS = _VALID_COMMON_ACTIONS

_VALID_CONTIG_CONTAINER_ACTIONS = _VALID_COMMON_ACTIONS | \
    {
        _CONTIG_CONTAINER_SWAP,
        _CONTIG_CONTAINER_ARRIVE,
        _CONTIG_CONTAINER_DEPART,
        _CONTIG_CONTAINER_BOOKENDS
    }

_VALID_SPARSE_CONTAINER_ACTIONS = _VALID_COMMON_ACTIONS | \
    {
        _SPARSE_CONTAINER_EXCHANGE,
        _SPARSE_CONTAINER_REMOVE
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

    if action == _DISABLED:
        resources.handler.HandleScalarDisabled(context)
    elif action == _QUIETED:
        resources.handler.HandleScalarQuieted(context)
    elif action == _CARRY:
        resources.handler.HandleScalarCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)
        deserialized = type_deserializer.Deserialize(resources.buf)

        if action == _ENABLED:
            resources.handler.HandleScalarEnabled(context, deserialized)
        elif action == _AWAKENED:
            resources.handler.HandleScalarAwakened(context, deserialized)
        elif action == _FULL:
            resources.handler.HandleScalarFullDump(context, deserialized)

    return HandleCID

def HandleContigContainerAction(resources, context, action):
    assert action in _VALID_CONTIG_CONTAINER_ACTIONS

    if action == _DISABLED:
        resources.handler.HandleContigContainerDisabled(context)
    elif action == _QUIETED:
        resources.handler.HandleContigContainerQuieted(context)
    elif action == _CARRY:
        resources.handler.HandleContigContainerCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)

        if action in (_ENABLED, _AWAKENED, _FULL):
            size = resources.buf.Read('H')
            deserialized = []
            for _ in range(size):
                deserialized.append(type_deserializer.Deserialize(resources.buf))

            if action == _ENABLED:
                resources.handler.HandleContigContainerEnabled(context, deserialized)
            elif action == _AWAKENED:
                resources.handler.HandleContigContainerAwakened(context, deserialized)
            elif action == _FULL:
                resources.handler.HandleContigContainerFullDump(context, deserialized)

        elif action == _CONTIG_CONTAINER_SWAP:
            bin_idx = resources.buf.Read('H')
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerSwap(context, bin_idx, deserialized)

        elif action == _CONTIG_CONTAINER_ARRIVE:
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerArrival(context, deserialized)

        elif action == _CONTIG_CONTAINER_DEPART:
            resources.handler.HandleContigContainerDeparture(context)

        elif action == _CONTIG_CONTAINER_BOOKENDS:
            deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleContigContainerBookends(context, deserialized)

    return HandleCID

def HandleSparseContainerAction(resources, context, action):
    assert action in _VALID_SPARSE_CONTAINER_ACTIONS

    if action == _DISABLED:
        resources.handler.HandleSparseContainerDisabled(context)
    elif action == _QUIETED:
        resources.handler.HandleSparseContainerQuieted(context)
    elif action == _CARRY:
        resources.handler.HandleSparseContainerCarried(context)
    else:
        type_deserializer = resources.GetDeserializer(context.current_cid)

        if action in (_ENABLED, _AWAKENED, _FULL):
            size = resources.buf.Read('H')
            deserialized = {}
            for _ in range(size):
                bin_idx = resources.buf.Read('H')
                bin_deserialized = type_deserializer.Deserialize(resources.buf)
                deserialized[bin_idx] = bin_deserialized

            if action == _ENABLED:
                resources.handler.HandleSparseContainerEnabled(context, deserialized)
            elif action == _AWAKENED:
                resources.handler.HandleSparseContainerAwakened(context, deserialized)
            elif action == _FULL:
                resources.handler.HandleSparseContainerFullDump(context, deserialized)

        elif action == _SPARSE_CONTAINER_EXCHANGE:
            bin_idx = resources.buf.Read('H')
            bin_deserialized = type_deserializer.Deserialize(resources.buf)
            resources.handler.HandleSparseContainerExchangedBin(context, bin_idx, bin_deserialized)

        elif action == _SPARSE_CONTAINER_REMOVE:
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

    def Iterate(self, handler: BlobHandler, time_range: Any = None):
        cursor = self.connection.cursor()
        if time_range is None:
            cmd = 'SELECT MIN(Timestamp),MAX(Timestamp) FROM Timestamps'
            cursor.execute(cmd)
            lo, hi = cursor.fetchone()
            time_range = [int(lo), int(hi)]

        if isinstance(time_range, int):
            time_range = [time_range, time_range]
        elif not isinstance(time_range, list):
            time_range = list(time_range)
            if len(time_range) == 1:
                time_range *= 2

        lo, hi = time_range[0], time_range[1]
        cmd = 'SELECT Id,Timestamp FROM Timestamps '
        cmd += f'WHERE CAST(Timestamp AS INTEGER)>={lo} AND CAST(Timestamp AS INTEGER)<={hi} '
        cmd += 'ORDER BY Id ASC'
        cursor.execute(cmd)
        timestamp_dict = OrderedDict(
            (tsid, int(ts))
            for tsid, ts in cursor.fetchall()
        )

        placeholders = ",".join("?" for _ in timestamp_dict.keys())
        cmd = (
            "SELECT TimestampID,Records FROM CollectionRecords "
            f"WHERE TimestampID IN ({placeholders}) ORDER BY TimestampID ASC"
        )
        cursor.execute(cmd, tuple(sorted(timestamp_dict.keys())))
        records_dict = OrderedDict(
            (tsid, record)
            for tsid,record in cursor.fetchall()
        )

        resources = Resources(self._dtype_inspector, self._simhier, handler)
        context = Context()
        for tsid,ts in timestamp_dict.items():
            compressed_blob = records_dict[tsid]
            resources.buf = ByteBuffer(zlib.decompress(compressed_blob))
            context.current_tick = ts

            handler_func = HandleCID
            while handler_func:
                handler_func = handler_func(resources, context)
            handler.SnapshotTick(context)

        self._final_tick = context.current_tick

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

    # Tick provided? Use [tick-heartbeat+1 : tick] time range with data extraction handler.
    else:
        cursor = dtype_inspector.connection.cursor()
        cursor.execute('SELECT Heartbeat FROM CollectionGlobals')
        heartbeat = cursor.fetchone()[0]
        tick_range = [args.tick-heartbeat+1, args.tick]

        handler = DataExtractionHandler(simhier)
        iterator.Iterate(handler, tick_range)
        print(handler.GetAllFinalValues())

if __name__ == '__main__':
    main()
