import os, sys, zlib
from typing import Any
from functools import partial
from abc import abstractmethod

# Allow running this file directly as a script. The package root (the directory
# containing the "viewer" package) is two levels up from this file:
#   python/argos/viewer/model/blob_parser.py -> python/argos
_PACKAGE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, _PACKAGE_ROOT)

from viewer.model.data_deserializers import ByteBuffer

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
    def __init__(self, initial_active_cids=None):
        self.current_cid = None
        self.current_tick = None

        # If None, we are looking for all CIDs. If empty set, we
        # have nothing left to look for. If non-empty set, we are
        # still looking for / parsing those CID bytes.
        self._active_cids = initial_active_cids

    def IsActive(self, cid):
        if self._active_cids is None:
            return True
        assert isinstance(self._active_cids, set)
        return cid in self._active_cids

    def Done(self):
        return self._active_cids == set()

class IteratorHandler:
    @abstractmethod
    def HandleScalarDisabled(self, context): pass

    @abstractmethod
    def HandleScalarQuieted(self, context): pass

    @abstractmethod
    def HandleScalarCarried(self, context): pass

    @abstractmethod
    def HandleScalarEnabled(self, context, deserialized): pass

    @abstractmethod
    def HandleScalarAwakened(self, context, deserialized): pass

    @abstractmethod
    def HandleScalarFullDump(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerDisabled(self, context): pass

    @abstractmethod
    def HandleContigContainerQuieted(self, context): pass

    @abstractmethod
    def HandleContigContainerCarried(self, context): pass

    @abstractmethod
    def HandleContigContainerEnabled(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerAwakened(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerFullDump(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerSwap(self, context, bin_idx, deserialized): pass

    @abstractmethod
    def HandleContigContainerArrival(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerDeparture(self, context): pass

    @abstractmethod
    def HandleContigContainerBookends(self, context, deserialized): pass

    @abstractmethod
    def HandleSparseContainerDisabled(self, context): pass

    @abstractmethod
    def HandleSparseContainerQuieted(self, context): pass

    @abstractmethod
    def HandleSparseContainerCarried(self, context): pass

    @abstractmethod
    def HandleSparseContainerEnabled(self, context, deserialized): pass

    @abstractmethod
    def HandleSparseContainerAwakened(self, context, deserialized): pass

    @abstractmethod
    def HandleSparseContainerFullDump(self, context, deserialized): pass

    @abstractmethod
    def HandleSparseContainerExchangedBin(self, context, bin_idx, bin_deserialized): pass

    @abstractmethod
    def HandleSparseContainerRemovedBin(self, context, bin_idx): pass

class SmokeTestHandler(IteratorHandler):
    def HandleScalarDisabled(self, context):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was disabled')

    def HandleScalarQuieted(self, context):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was quieted')

    def HandleScalarCarried(self, context):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was carried')

    def HandleScalarEnabled(self, context, deserialized):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was enabled')

    def HandleScalarAwakened(self, context, deserialized):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was awakened')

    def HandleScalarFullDump(self, context, deserialized):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was fully dumped')

    def HandleContigContainerDisabled(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was disabled')

    def HandleContigContainerQuieted(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was quieted')

    def HandleContigContainerCarried(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was carried')

    def HandleContigContainerEnabled(self, context, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was enabled')

    def HandleContigContainerAwakened(self, context, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was awakened')

    def HandleContigContainerFullDump(self, context, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was fully dumped')

    def HandleContigContainerSwap(self, context, bin_idx, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} swapped bin {bin_idx}')

    def HandleContigContainerArrival(self, context, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} had an arrival')

    def HandleContigContainerDeparture(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} had a departure')

    def HandleContigContainerBookends(self, context, deserialized):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} updated its bookends')

    def HandleSparseContainerDisabled(self, context):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was disabled')

    def HandleSparseContainerQuieted(self, context):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was quieted')

    def HandleSparseContainerCarried(self, context):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was carried')

    def HandleSparseContainerEnabled(self, context, deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was enabled')

    def HandleSparseContainerAwakened(self, context, deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was awakened')

    def HandleSparseContainerFullDump(self, context, deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was fully dumped')

    def HandleSparseContainerExchangedBin(self, context, bin_idx, bin_deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} exchanged bin {bin_idx}')

    def HandleSparseContainerRemovedBin(self, context, bin_idx):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} removed bin {bin_idx}')

def HandleCID(resources, context):
    if resources.buf.Done() or context.Done():
        return

    cid = resources.buf.Read('H')
    if not context.IsActive(cid):
        raise 'TODO XXX: skip over bytes'

    context.current_cid = cid
    return HandleAction

def HandleAction(resources, context):
    cid = context.current_cid
    simhier = resources.simhier
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

    @property
    def connection(self):
        return self._dtype_inspector.connection

    def Iterate(self, handler: IteratorHandler, time_range: Any = None):
        cursor = self.connection.cursor()
        if time_range is None:
            cmd = 'SELECT MIN(Timestamp),MAX(Timestamp) FROM Timestamps'
            cursor.execute(cmd)
            lo, hi = cursor.fetchone()
            time_range = [int(lo), int(hi)]

        if not type(time_range) in (list, tuple):
            time_range = [int(time_range)]

        time_range = list(time_range)
        if len(time_range) == 1:
            time_range = [time_range, time_range]

        lo, hi = time_range[0], time_range[1]
        cmd = 'SELECT Id,Timestamp FROM Timestamps '
        cmd += f'WHERE CAST(Timestamp AS INTEGER)>={lo} AND CAST(Timestamp AS INTEGER)<={hi}'
        cursor.execute(cmd)
        timestamp_dict = {tsid:int(ts) for tsid,ts in cursor.fetchall()}

        placeholders = ",".join("?" for _ in timestamp_dict.keys())
        cmd = (
            "SELECT TimestampID,Records FROM CollectionRecords "
            f"WHERE TimestampID IN ({placeholders}) ORDER BY TimestampID ASC"
        )
        cursor.execute(cmd, tuple(sorted(timestamp_dict.keys())))
        records_dict = {tsid:record for tsid,record in cursor.fetchall()}

        resources = Resources(self._dtype_inspector, self._simhier, handler)
        context = Context()
        for tsid,ts in timestamp_dict.items():
            compressed_blob = records_dict[tsid]
            resources.buf = ByteBuffer(zlib.decompress(compressed_blob))
            context.current_tick = ts

            handler_func = HandleCID
            while handler_func:
                handler_func = handler_func(resources, context)

def main():
    from viewer.model.dtype_inspector import DataTypeInspector
    from viewer.model.simhier import SimHierarchy

    db_file = sys.argv[1]
    dtype_inspector = DataTypeInspector(db_file)
    simhier = SimHierarchy(dtype_inspector.connection, dtype_inspector)
    iterator = BlobIterator(dtype_inspector, simhier)

    handler = SmokeTestHandler()
    iterator.Iterate(handler)

if __name__ == '__main__':
    main()
