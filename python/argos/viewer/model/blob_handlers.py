import copy
from abc import abstractmethod
from collections import OrderedDict

# Base class for all blob handlers (for blob iteration)
class BlobHandler:
    @abstractmethod
    def HandleScalarClosed(self, context): pass

    @abstractmethod
    def HandleScalarCarried(self, context): pass

    @abstractmethod
    def HandleScalarFullDump(self, context, deserialized): pass

    @abstractmethod
    def HandleContigContainerClosed(self, context): pass

    @abstractmethod
    def HandleContigContainerCarried(self, context): pass

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
    def HandleSparseContainerClosed(self, context): pass

    @abstractmethod
    def HandleSparseContainerCarried(self, context): pass

    @abstractmethod
    def HandleSparseContainerFullDump(self, context, deserialized): pass

    @abstractmethod
    def HandleSparseContainerExchangedBin(self, context, bin_idx, bin_deserialized): pass

    @abstractmethod
    def HandleSparseContainerRemovedBin(self, context, bin_idx): pass

    @abstractmethod
    def HandleSparseContainerAddedBin(self, context, bin_idx, bin_deserialized): pass

    # Called by the blob iterator after each blob (tick) has been fully
    # processed. Handlers that track per-tick state can override this.
    def SnapshotTick(self, context): pass

# Blob handler for sanity checking (ensure that we can simply iterate over any blob without parsing bugs)
class SmokeTestHandler(BlobHandler):
    def HandleScalarClosed(self, context):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was closed')

    def HandleScalarCarried(self, context):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was carried')

    def HandleScalarFullDump(self, context, deserialized):
        print(f'At tick {context.current_tick}, scalar cid {context.current_cid} was fully dumped')

    def HandleContigContainerClosed(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was closed')

    def HandleContigContainerCarried(self, context):
        print(f'At tick {context.current_tick}, contig container cid {context.current_cid} was carried')

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

    def HandleSparseContainerClosed(self, context):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was closed')

    def HandleSparseContainerCarried(self, context):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was carried')

    def HandleSparseContainerFullDump(self, context, deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} was fully dumped')

    def HandleSparseContainerExchangedBin(self, context, bin_idx, bin_deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} exchanged bin {bin_idx}')

    def HandleSparseContainerRemovedBin(self, context, bin_idx):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} removed bin {bin_idx}')

    def HandleSparseContainerAddedBin(self, context, bin_idx, bin_deserialized):
        print(f'At tick {context.current_tick}, sparse container cid {context.current_cid} added bin {bin_idx}')

# Blob handler for extracting collected values/structs/containers
class DataExtractionHandler(BlobHandler):
    def __init__(self, simhier, snapshot_cids=None):
        self._simhier = simhier
        self._values_by_cid = {}

        # When snapshot_cids is None, per-tick tracking is disabled and
        # SnapshotTick() is a no-op (zero overhead for callers that only
        # need the final reconstructed value). When a set of CIDs is given,
        # we record a deep copy of each of those CIDs' values at every tick.
        self._snapshot_cids = snapshot_cids
        self._values_by_cid_by_tick = {}

    def HandleScalarClosed(self, context):
        if context.current_cid in self._values_by_cid:
            self._values_by_cid[context.current_cid] = None

    def HandleScalarCarried(self, context):
        pass

    def HandleScalarFullDump(self, context, deserialized):
        self._values_by_cid[context.current_cid] = deserialized

    def HandleContigContainerClosed(self, context):
        self._values_by_cid[context.current_cid] = []

    def HandleContigContainerCarried(self, context):
        pass

    def HandleContigContainerFullDump(self, context, deserialized):
        assert isinstance(deserialized, list)
        assert len(deserialized) <= self._simhier.GetCapacityByCollectionID(context.current_cid)
        self._values_by_cid[context.current_cid] = deserialized

    def HandleContigContainerSwap(self, context, bin_idx, deserialized):
        if context.current_cid in self._values_by_cid:
            container_values = self._values_by_cid[context.current_cid]
            assert bin_idx < self._simhier.GetCapacityByCollectionID(context.current_cid)
            assert bin_idx < len(container_values)
            container_values[bin_idx] = deserialized

    def HandleContigContainerArrival(self, context, deserialized):
        if context.current_cid in self._values_by_cid:
            container_values = self._values_by_cid[context.current_cid]
            container_values.append(deserialized)
            assert len(container_values) <= self._simhier.GetCapacityByCollectionID(context.current_cid)

    def HandleContigContainerDeparture(self, context):
        if context.current_cid in self._values_by_cid:
            container_values = self._values_by_cid[context.current_cid]
            assert len(container_values) > 0
            container_values.pop(0)

    def HandleContigContainerBookends(self, context, deserialized):
        self.HandleContigContainerDeparture(context)
        self.HandleContigContainerArrival(context, deserialized)

    def HandleSparseContainerClosed(self, context):
        if context.current_cid in self._values_by_cid:
            self._values_by_cid[context.current_cid] = None

    def HandleSparseContainerCarried(self, context):
        pass

    def HandleSparseContainerFullDump(self, context, deserialized):
        self._ResetSparseContainer(context, deserialized)

    def HandleSparseContainerExchangedBin(self, context, bin_idx, bin_deserialized):
        if context.current_cid in self._values_by_cid:
            assert bin_idx < len(self._values_by_cid[context.current_cid])
            self._values_by_cid[context.current_cid][bin_idx] = bin_deserialized

    def HandleSparseContainerRemovedBin(self, context, bin_idx):
        self.HandleSparseContainerExchangedBin(context, bin_idx, None)

    def HandleSparseContainerAddedBin(self, context, bin_idx, bin_deserialized):
        if context.current_cid in self._values_by_cid:
            assert bin_idx < len(self._values_by_cid[context.current_cid])
            self._values_by_cid[context.current_cid][bin_idx] = bin_deserialized

    def SnapshotTick(self, context):
        if self._snapshot_cids is None:
            return

        for cid in self._snapshot_cids:
            if cid not in self._values_by_cid:
                continue

            # Deep copy is required because contig/sparse container values are
            # lists mutated in place Without copying, every tick's snapshot
            # would alias the same mutated list.
            value = copy.deepcopy(self._values_by_cid[cid])
            if cid not in self._values_by_cid_by_tick:
                self._values_by_cid_by_tick[cid] = OrderedDict()
            self._values_by_cid_by_tick[cid][context.current_tick] = value

    def HasDataFor(self, ident):
        cid = self._ResolveCID(ident)
        return cid in self._values_by_cid

    def GetValuesByTick(self, ident):
        cid = self._ResolveCID(ident)
        return self._values_by_cid_by_tick.get(cid, OrderedDict())

    def GetFinalValue(self, ident):
        cid = self._ResolveCID(ident)
        return self._values_by_cid.get(cid)

    def GetAllFinalValues(self, use_path_keys=True):
        elem_paths = self._simhier.GetItemElemPaths()
        if use_path_keys:
            identifiers = elem_paths
        else:
            identifiers = [self._simhier.GetCollectionID(path) for path in elem_paths]

        final_values = {
            ident:self.GetFinalValue(ident)
            for ident in identifiers
        }

        return final_values

    def _ResolveCID(self, ident):
        if isinstance(ident, str):
            try:
                ident = int(ident)
            except:
                ident = self._simhier.GetCollectionID(ident)

        assert isinstance(ident, int)
        return ident

    def _ResetSparseContainer(self, context, deserialized):
        capacity = self._simhier.GetCapacityByCollectionID(context.current_cid)
        self._values_by_cid[context.current_cid] = [None]*capacity
        for bin_idx, bin_deserialized in deserialized.items():
            self._values_by_cid[context.current_cid][bin_idx] = bin_deserialized
