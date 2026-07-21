// <Checkpoint.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"
#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/utils/ValidValue.hpp"

namespace simdb::argos {

//! Encodings that C++ and python agree on. The python deserializers interpret
//! collected data using the header:
//!
//!   [uint16_t cid]     // collectable ID
//!   [uint8_t action]   // encoding
//!
//! See CheckpointEncodings.hpp for full action / byte layout documentation.
enum class Action : uint8_t {
    CLOSED = 0x00,
    FULL = 0x01,
    CARRY = 0x02,

    CONTAINER_SWAP = 0x10,
    CONTAINER_MULTI_SWAP = 0x11,

    CONTIG_ARRIVE = 0x20,
    CONTIG_DEPART = 0x21,
    CONTIG_BOOKENDS = 0x22,
    CONTIG_MIMO = 0x23,

    SPARSE_REMOVE = 0x30,
    SPARSE_ADD = 0x31,
    SPARSE_MULTI_REMOVE = 0x32,
};

//! \class CollectableCheckpoint
class CollectableCheckpoint
{
public:
    /// Destroys this checkpoint node.
    virtual ~CollectableCheckpoint() = default;

    /// Returns the previous checkpoint in the chain, or nullptr.
    CollectableCheckpoint* getPrev() const { return prev_.get(); }

    /// Returns the next checkpoint in the chain, or nullptr.
    CollectableCheckpoint* getNext() const { return next_; }

    /// Returns the collection window that created this checkpoint.
    uint64_t getWindowID() const { return window_id_; }

    /// Drops the link to the previous checkpoint.
    void detachPrev() { prev_.reset(); }

    /// Returns the shared_ptr to the previous checkpoint, if any.
    const std::shared_ptr<CollectableCheckpoint>& getPrevShared() const { return prev_; }

protected:
    /// Links \p prev to this node and stores \p window_id.
    CollectableCheckpoint(std::shared_ptr<CollectableCheckpoint> prev, uint64_t window_id) :
        prev_(prev),
        window_id_(window_id)
    {
        if (prev_)
        {
            prev_->next_ = this;
        }
    }

    /// Returns true if a CLOSED event occurred between \p data_prev and \p self.
    template <typename CheckpointT>
    static bool closedSincePrevDataCheckpoint_(const CheckpointT* self, const CheckpointT* data_prev)
    {
        if (!data_prev)
        {
            return false;
        }
        const CheckpointT* checkpoint = self->prev();
        while (checkpoint && checkpoint != data_prev)
        {
            if (checkpoint->isClosedEvent())
            {
                return true;
            }
            checkpoint = checkpoint->prev();
        }
        return false;
    }

    std::shared_ptr<CollectableCheckpoint> prev_;
    CollectableCheckpoint* next_ = nullptr;
    const uint64_t window_id_;
};

class ScalarCheckpoint final : public CollectableCheckpoint
{
public:
    /// Creates a data checkpoint holding a full scalar snapshot.
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, const std::vector<char>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    /// Creates a lifecycle checkpoint recording a close transition.
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    /// Returns the previous scalar checkpoint in the chain.
    ScalarCheckpoint* prev() { return static_cast<ScalarCheckpoint*>(getPrev()); }

    /// Returns the previous scalar checkpoint in the chain.
    const ScalarCheckpoint* prev() const { return static_cast<const ScalarCheckpoint*>(getPrev()); }

    /// Returns the next scalar checkpoint in the chain.
    ScalarCheckpoint* next() { return static_cast<ScalarCheckpoint*>(getNext()); }

    /// Returns true when this node holds collected bytes rather than a lifecycle flag.
    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    /// Returns true when this node records a transition to closed.
    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    /// Returns true for any lifecycle (close) checkpoint.
    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    /// Encodes an unconditional FULL snapshot for heartbeat or absent-CID refresh.
    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        buf.append(full_bytes_);
        return encoded;
    }

    /// Encodes CLOSED, FULL, or CARRY by diffing against the prior data checkpoint.
    std::unique_ptr<CollectedData> encodeForPipeline(uint16_t cid, bool force_snapshot)
    {
        if (lifecycle_change_.isValid())
        {
            return encodeLifecycleEvt_(cid);
        }

        if (force_snapshot)
        {
            return encodeFull_(cid);
        }

        const auto* data_prev = prevDataCheckpoint_();
        if (!data_prev)
        {
            return encodeFull_(cid);
        }

        if (data_prev->full_bytes_ != full_bytes_)
        {
            return encodeFull_(cid);
        }

        if (closedSincePrevDataCheckpoint_(this, data_prev))
        {
            return encodeFull_(cid);
        }

        return encodeDelta_(cid);
    }

private:
    /// Walks backward to the nearest prior data checkpoint.
    const ScalarCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Encodes a CLOSED lifecycle wire record.
    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    /// Encodes a FULL snapshot of the current scalar bytes.
    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        assert(!full_bytes_.empty());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        buf.append(full_bytes_);
        return encoded;
    }

    /// Encodes CARRY when bytes are unchanged since the prior data checkpoint.
    std::unique_ptr<CollectedData> encodeDelta_(uint16_t cid)
    {
        const auto* data_prev = prevDataCheckpoint_();
        assert(data_prev && data_prev->full_bytes_ == full_bytes_ && !full_bytes_.empty());
        (void)data_prev;
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::CARRY);
        return encoded;
    }

    std::vector<char> full_bytes_;
    ValidValue<bool> lifecycle_change_;
};

class ContigContainerCheckpoint : public CollectableCheckpoint
{
public:
    /// Creates a data checkpoint holding a full contig container snapshot.
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id,
                              const std::vector<std::vector<char>>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    /// Creates a lifecycle checkpoint recording a close transition.
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id,
                              bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    /// Returns the previous contig checkpoint in the chain.
    ContigContainerCheckpoint* prev() { return static_cast<ContigContainerCheckpoint*>(getPrev()); }

    /// Returns the previous contig checkpoint in the chain.
    const ContigContainerCheckpoint* prev() const { return static_cast<const ContigContainerCheckpoint*>(getPrev()); }

    /// Returns the next contig checkpoint in the chain.
    ContigContainerCheckpoint* next() { return static_cast<ContigContainerCheckpoint*>(getNext()); }

    /// Returns true when this node holds collected bytes rather than a lifecycle flag.
    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    /// Returns true when this node records a transition to closed.
    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    /// Returns true for any lifecycle (close) checkpoint.
    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    /// Encodes an unconditional FULL snapshot for heartbeat or absent-CID refresh.
    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        assert(isDataCheckpoint());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendContigBins_(buf, full_bytes_);
        return encoded;
    }

    /// Encodes CLOSED, FULL, or a contig delta by diffing against the prior data checkpoint.
    std::unique_ptr<CollectedData> encodeForPipeline(uint16_t cid, bool force_snapshot)
    {
        if (lifecycle_change_.isValid())
        {
            return encodeLifecycleEvt_(cid);
        }

        if (force_snapshot)
        {
            return encodeFull_(cid);
        }

        auto* data_prev = prevDataCheckpoint_();
        if (!data_prev)
        {
            return encodeFull_(cid);
        }

        if (closedSincePrevDataCheckpoint_(this, data_prev))
        {
            return encodeFull_(cid);
        }

        const auto classification = classifyContigChange(data_prev->full_bytes_, full_bytes_);
        if (classification.kind == ContigDeltaKind::FULL)
        {
            return encodeFull_(cid);
        }

        return encodeDelta_(cid, classification);
    }

private:
    /// Walks backward to the nearest prior data checkpoint.
    const ContigContainerCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Maps a contig delta classification to its wire Action byte.
    static Action contigActionFromKind_(ContigDeltaKind kind)
    {
        switch (kind)
        {
        case ContigDeltaKind::CARRY:
            return Action::CARRY;
        case ContigDeltaKind::SWAP:
            return Action::CONTAINER_SWAP;
        case ContigDeltaKind::MULTI_SWAP:
            return Action::CONTAINER_MULTI_SWAP;
        case ContigDeltaKind::ARRIVE:
            return Action::CONTIG_ARRIVE;
        case ContigDeltaKind::DEPART:
            return Action::CONTIG_DEPART;
        case ContigDeltaKind::BOOKENDS:
            return Action::CONTIG_BOOKENDS;
        case ContigDeltaKind::MIMO:
            return Action::CONTIG_MIMO;
        case ContigDeltaKind::FULL:
            break;
        }
        throw DBException("Invalid contig delta kind");
    }

    /// Appends the FULL tail for the current contig snapshot to \p buf.
    void appendFullTail_(StreamBuffer& buf) const { appendContigBins_(buf, full_bytes_); }

    /// Appends contig bins as [count][bin bytes]... to \p buf.
    static void appendContigBins_(StreamBuffer& buf, const std::vector<std::vector<char>>& bins)
    {
        const auto size = countContigElements(bins);
        buf.append(size);
        for (uint16_t i = 0; i < size; ++i)
        {
            buf.append(bins[i]);
        }
    }

    /// Encodes a CLOSED lifecycle wire record.
    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        (void)cid;
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    /// Encodes a FULL snapshot of the current contig container.
    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

    /// Encodes a classified contig delta to the wire buffer.
    std::unique_ptr<CollectedData> encodeDelta_(uint16_t cid, const ContigDeltaClassification& classification)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        const auto action = contigActionFromKind_(classification.kind);
        buf.append(action);

        switch (action)
        {
        case Action::CONTAINER_SWAP:
            assert(classification.swap_index.isValid());
            assert(!classification.payload.empty());
            buf.append(classification.swap_index.getValue());
            buf.append(classification.payload);
            break;
        case Action::CONTAINER_MULTI_SWAP:
            assert(classification.swap_indices.size() >= 2);
            assert(classification.swap_indices.size() == classification.swap_payloads.size());
            buf.append(static_cast<uint8_t>(classification.swap_indices.size()));
            for (size_t i = 0; i < classification.swap_indices.size(); ++i)
            {
                buf.append(classification.swap_indices[i]);
                buf.append(classification.swap_payloads[i]);
            }
            break;
        case Action::CARRY:
            break;
        case Action::CONTIG_ARRIVE:
        case Action::CONTIG_BOOKENDS:
            assert(!classification.payload.empty());
            buf.append(classification.payload);
            break;
        case Action::CONTIG_MIMO:
            buf.append(classification.depart_count);
            buf.append(classification.arrive_count);
            for (const auto& arrive_payload : classification.arrive_payloads)
            {
                buf.append(arrive_payload);
            }
            break;
        case Action::CONTIG_DEPART:
            break;
        default:
            throw DBException("Unexpected contig delta action");
        }

        return encoded;
    }

    std::vector<std::vector<char>> full_bytes_;
    ValidValue<bool> lifecycle_change_;
};

class SparseContainerCheckpoint : public CollectableCheckpoint
{
public:
    /// Creates a data checkpoint holding a full sparse container snapshot.
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id,
                              const std::map<uint16_t, std::vector<char>>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    /// Creates a lifecycle checkpoint recording a close transition.
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id,
                              bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    /// Returns the previous sparse checkpoint in the chain.
    SparseContainerCheckpoint* prev() { return static_cast<SparseContainerCheckpoint*>(getPrev()); }

    /// Returns the previous sparse checkpoint in the chain.
    const SparseContainerCheckpoint* prev() const { return static_cast<const SparseContainerCheckpoint*>(getPrev()); }

    /// Returns the next sparse checkpoint in the chain.
    SparseContainerCheckpoint* next() { return static_cast<SparseContainerCheckpoint*>(getNext()); }

    /// Returns true when this node holds collected bytes rather than a lifecycle flag.
    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    /// Returns true when this node records a transition to closed.
    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    /// Returns true for any lifecycle (close) checkpoint.
    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    /// Encodes an unconditional FULL snapshot for heartbeat or absent-CID refresh.
    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        assert(isDataCheckpoint());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

    /// Encodes CLOSED, FULL, or a sparse delta by diffing against the prior data checkpoint.
    std::unique_ptr<CollectedData> encodeForPipeline(uint16_t cid, bool force_snapshot)
    {
        if (lifecycle_change_.isValid())
        {
            return encodeLifecycleEvt_(cid);
        }

        if (force_snapshot)
        {
            return encodeFull_(cid);
        }

        const auto* data_prev = prevDataCheckpoint_();
        if (!data_prev)
        {
            return encodeFull_(cid);
        }

        if (closedSincePrevDataCheckpoint_(this, data_prev))
        {
            return encodeFull_(cid);
        }

        const auto classification = classifySparseChange(data_prev->full_bytes_, full_bytes_);
        if (classification.kind == SparseDeltaKind::FULL)
        {
            return encodeFull_(cid);
        }

        return encodeDelta_(cid, classification);
    }

private:
    /// Walks backward to the nearest prior data checkpoint.
    const SparseContainerCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Maps a sparse delta classification to its wire Action byte.
    static Action sparseActionFromKind_(SparseDeltaKind kind)
    {
        switch (kind)
        {
        case SparseDeltaKind::CARRY:
            return Action::CARRY;
        case SparseDeltaKind::SWAP:
            return Action::CONTAINER_SWAP;
        case SparseDeltaKind::MULTI_SWAP:
            return Action::CONTAINER_MULTI_SWAP;
        case SparseDeltaKind::REMOVE:
            return Action::SPARSE_REMOVE;
        case SparseDeltaKind::ADD:
            return Action::SPARSE_ADD;
        case SparseDeltaKind::MULTI_REMOVE:
            return Action::SPARSE_MULTI_REMOVE;
        case SparseDeltaKind::FULL:
            break;
        }
        throw DBException("Invalid sparse delta kind");
    }

    /// Appends the FULL tail for the current sparse snapshot to \p buf.
    void appendFullTail_(StreamBuffer& buf) const { appendSparseBins_(buf, full_bytes_); }

    /// Appends sparse bins as [count][idx][bin bytes]... to \p buf.
    static void appendSparseBins_(StreamBuffer& buf, const std::map<uint16_t, std::vector<char>>& bins)
    {
        const auto size = countSparseElements(bins);
        buf.append(size);
        for (const auto& [bin_idx, bin_bytes] : bins)
        {
            if (!bin_bytes.empty())
            {
                buf.append(bin_idx);
                buf.append(bin_bytes);
            }
        }
    }

    /// Encodes a CLOSED lifecycle wire record.
    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        (void)cid;
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    /// Encodes a FULL snapshot of the current sparse container.
    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

    /// Encodes a classified sparse delta to the wire buffer.
    std::unique_ptr<CollectedData> encodeDelta_(uint16_t cid, const SparseDeltaClassification& classification)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        const auto action = sparseActionFromKind_(classification.kind);
        buf.append(action);

        switch (action)
        {
        case Action::CONTAINER_SWAP:
            assert(classification.bin_index.isValid());
            assert(!classification.payload.empty());
            buf.append(classification.bin_index.getValue());
            buf.append(classification.payload);
            break;
        case Action::CONTAINER_MULTI_SWAP:
            assert(classification.bin_indices.size() >= 2);
            assert(classification.bin_indices.size() == classification.payloads.size());
            buf.append(static_cast<uint8_t>(classification.bin_indices.size()));
            for (size_t i = 0; i < classification.bin_indices.size(); ++i)
            {
                buf.append(classification.bin_indices[i]);
                buf.append(classification.payloads[i]);
            }
            break;
        case Action::SPARSE_REMOVE:
            assert(classification.bin_index.isValid());
            buf.append(classification.bin_index.getValue());
            break;
        case Action::SPARSE_ADD:
            assert(classification.bin_index.isValid());
            assert(!classification.payload.empty());
            buf.append(classification.bin_index.getValue());
            buf.append(classification.payload);
            break;
        case Action::SPARSE_MULTI_REMOVE:
            assert(classification.bin_indices.size() >= 2);
            buf.append(static_cast<uint8_t>(classification.bin_indices.size()));
            for (const auto idx : classification.bin_indices)
            {
                buf.append(idx);
            }
            break;
        case Action::CARRY:
            break;
        default:
            throw DBException("Unexpected sparse delta action");
        }

        return encoded;
    }

    std::map<uint16_t, std::vector<char>> full_bytes_;
    ValidValue<bool> lifecycle_change_;
};

} // namespace simdb::argos
