from abc import abstractmethod

class BlobHandler:
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

class SmokeTestHandler(BlobHandler):
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
