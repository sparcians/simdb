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
enum class Action : uint8_t {
    // Tier 1: 0x00–0x0F — lifecycle / common (scalars + all collectables)
    CLOSED = 0x00,
    FULL = 0x01,
    CARRY = 0x02,
    // 0x03–0x0F reserved

    // Tier 2: 0x10–0x1F — any-container (identical wire for contig & sparse)
    CONTAINER_SWAP = 0x10,
    CONTAINER_MULTI_SWAP = 0x11,
    // 0x12–0x1F reserved

    // Tier 3: 0x20–0x2F — contig-specific (FIFO queue semantics)
    CONTIG_ARRIVE = 0x20,
    CONTIG_DEPART = 0x21,
    CONTIG_BOOKENDS = 0x22,
    CONTIG_MIMO = 0x23,
    // 0x24–0x2F reserved

    // Tier 4: 0x30–0x3F — sparse-specific (explicit bin indices)
    SPARSE_REMOVE = 0x30,
    SPARSE_ADD = 0x31,
    SPARSE_MULTI_REMOVE = 0x32,
    // 0x33–0x3F reserved (SPARSE_BATCH future)
};

//! \class CollectableCheckpoint
//! \brief Base class for all collectable checkpoints.
class CollectableCheckpoint
{
public:
    virtual ~CollectableCheckpoint() = default;

    CollectableCheckpoint* getPrev() const { return prev_.get(); }

    CollectableCheckpoint* getNext() const { return next_; }

    uint64_t getWindowID() const { return window_id_; }

    void detachPrev() { prev_.reset(); }

    const std::shared_ptr<CollectableCheckpoint>& getPrevShared() const { return prev_; }

protected:
    CollectableCheckpoint(std::shared_ptr<CollectableCheckpoint> prev, uint64_t window_id) :
        prev_(prev),
        window_id_(window_id)
    {
        if (prev_)
        {
            prev_->next_ = this;
        }
    }

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
    // Use this ctor for data collection events
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, const std::vector<char>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    // Use this ctor for close lifecycle events
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    ScalarCheckpoint* prev() { return static_cast<ScalarCheckpoint*>(getPrev()); }

    const ScalarCheckpoint* prev() const { return static_cast<const ScalarCheckpoint*>(getPrev()); }

    ScalarCheckpoint* next() { return static_cast<ScalarCheckpoint*>(getNext()); }

    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        buf.append(full_bytes_);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeForPipeline(uint16_t cid, bool force_snapshot)
    {
        // ScalarCheckpoints only return:
        //   CLOSED
        //   FULL
        //   CARRY
        //
        // The incoming force_snapshot flag is true when we require a heartbeat refresh.
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
    const ScalarCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        (void)cid;
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        assert(!full_bytes_.empty());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        buf.append(full_bytes_);
        return encoded;
    }

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
    // Use this ctor for data collection events
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id,
                              const std::vector<std::vector<char>>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    // Use this ctor for close lifecycle events
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id,
                              bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    ContigContainerCheckpoint* prev() { return static_cast<ContigContainerCheckpoint*>(getPrev()); }

    const ContigContainerCheckpoint* prev() const { return static_cast<const ContigContainerCheckpoint*>(getPrev()); }

    ContigContainerCheckpoint* next() { return static_cast<ContigContainerCheckpoint*>(getNext()); }

    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        assert(isDataCheckpoint());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendContigBins_(buf, full_bytes_);
        return encoded;
    }

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
    const ContigContainerCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

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

    void appendFullTail_(StreamBuffer& buf) const { appendContigBins_(buf, full_bytes_); }

    static void appendContigBins_(StreamBuffer& buf, const std::vector<std::vector<char>>& bins)
    {
        const auto size = countContigElements(bins);
        buf.append(size);
        for (uint16_t i = 0; i < size; ++i)
        {
            buf.append(bins[i]);
        }
    }

    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        (void)cid;
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

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
        case Action::CARRY:
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
    // Use this ctor for data collection events
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id,
                              const std::map<uint16_t, std::vector<char>>& full_bytes) :
        CollectableCheckpoint(prev, window_id),
        full_bytes_(full_bytes)
    {
    }

    // Use this ctor for close lifecycle events
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id,
                              bool switched_to_closed) :
        CollectableCheckpoint(prev, window_id),
        lifecycle_change_(switched_to_closed)
    {
    }

    SparseContainerCheckpoint* prev() { return static_cast<SparseContainerCheckpoint*>(getPrev()); }

    const SparseContainerCheckpoint* prev() const { return static_cast<const SparseContainerCheckpoint*>(getPrev()); }

    SparseContainerCheckpoint* next() { return static_cast<SparseContainerCheckpoint*>(getNext()); }

    bool isDataCheckpoint() const { return !lifecycle_change_.isValid(); }

    bool isClosedEvent() const { return lifecycle_change_.isValid() && lifecycle_change_.getValue(); }

    bool isLifecycleEvent() const { return lifecycle_change_.isValid(); }

    std::unique_ptr<CollectedData> encodeSnapshotForPipeline(uint16_t cid) const
    {
        assert(isDataCheckpoint());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

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
    const SparseContainerCheckpoint* prevDataCheckpoint_() const
    {
        const auto* checkpoint = prev();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

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

    void appendFullTail_(StreamBuffer& buf) const { appendSparseBins_(buf, full_bytes_); }

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

    std::unique_ptr<CollectedData> encodeLifecycleEvt_(uint16_t cid)
    {
        assert(lifecycle_change_.isValid() && lifecycle_change_.getValue());
        (void)cid;
        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return encoded;
    }

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
