// <Checkpointer.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpoint.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <vector>

namespace simdb::argos {

namespace detail {
/// Counts occupied front-packed bins in a contig snapshot.
inline uint16_t getContainerSize(const std::vector<std::vector<char>>& contig_bin_bytes)
{
    uint64_t size = 0;
    for (const auto& bin_bytes : contig_bin_bytes)
    {
        if (!bin_bytes.empty())
        {
            ++size;
        } else
        {
            break;
        }
    }
    assert(size <= UINT16_MAX);
    return size;
}

/// Counts occupied bins in a sparse snapshot.
inline uint16_t getContainerSize(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
{
    uint64_t size = 0;
    for (const auto& [_, bin_bytes] : sparse_bin_bytes)
    {
        if (!bin_bytes.empty())
        {
            ++size;
        }
    }
    assert(size <= UINT16_MAX);
    return size;
}
} // namespace detail

//! \class CollectableCheckpointer
class CollectableCheckpointer
{
public:
    /// Destroys the checkpointer and its checkpoint chain.
    virtual ~CollectableCheckpointer() = default;

    /// Appends a scalar snapshot checkpoint; \throws DBException on unsupported checkpointer types.
    virtual void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes)
    {
        (void)window_id;
        (void)scalar_bytes;
        throw DBException("Not implemented");
    }

    /// Appends a contig container snapshot checkpoint; \throws DBException on unsupported checkpointer types.
    virtual void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        (void)window_id;
        (void)contig_bin_bytes;
        throw DBException("Not implemented");
    }

    /// Appends a sparse container snapshot checkpoint; \throws DBException on unsupported checkpointer types.
    virtual void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        (void)window_id;
        (void)sparse_bin_bytes;
        throw DBException("Not implemented");
    }

    /// Records a closed lifecycle event at \p window_id.
    virtual void closeRecord(uint64_t window_id) = 0;

    /// Encodes wire records for \p cid at \p sim_time, using the checkpoint for \p window_id when present.
    virtual std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                          uint16_t cid) = 0;

    /// Returns true if this collectable staged data in \p window_id.
    virtual bool participatedInWindow(uint64_t window_id) const = 0;

    /// Writes collectable-specific metadata after simulation teardown; default is a no-op.
    virtual void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager*) { (void)cid; }

protected:
    /// Stores the heartbeat interval used to force periodic FULL snapshots.
    explicit CollectableCheckpointer(size_t heartbeat) :
        heartbeat_(heartbeat)
    {
    }

    /// Finds the latest checkpoint in \p tail at or before \p window_id.
    template <typename CheckpointT>
    static CheckpointT* getAnchorForWindow_(uint64_t window_id, const std::shared_ptr<CheckpointT>& tail)
    {
        CheckpointT* anchor = tail.get();
        CheckpointT* best = nullptr;
        while (anchor)
        {
            if (anchor->getWindowID() > window_id)
            {
                break;
            }
            best = anchor;
            anchor = anchor->next();
        }
        return best;
    }

    /// Returns true if \p tail contains a checkpoint whose window equals \p window_id.
    template <typename CheckpointT>
    static bool participatedInWindow_(uint64_t window_id, const std::shared_ptr<CheckpointT>& tail)
    {
        if (!tail)
        {
            return false;
        }
        auto* anchor = getAnchorForWindow_(window_id, tail);
        return anchor && anchor->getWindowID() == window_id;
    }

    /// Locates the shared_ptr in \p head that owns \p raw.
    template <typename CheckpointT>
    static std::shared_ptr<CheckpointT> getSharedCheckpoint_(CheckpointT* raw, const std::shared_ptr<CheckpointT>& head)
    {
        std::shared_ptr<CheckpointT> sp = head;
        while (sp)
        {
            if (sp.get() == raw)
            {
                return sp;
            }
            auto prev = sp->getPrevShared();
            if (!prev)
            {
                return nullptr;
            }
            sp = std::static_pointer_cast<CheckpointT>(prev);
        }
        return nullptr;
    }

    /// Reads the action byte from an encoded wire record (after the CID prefix).
    static Action readEncodedAction_(const CollectedData& encoded)
    {
        return static_cast<Action>(encoded.getData().at(sizeof(uint16_t)));
    }

    /// Wraps a single encoded record in a one-element vector.
    template <typename T> static std::vector<T> makeSingleElemVector_(T&& only)
    {
        std::vector<T> out;
        out.emplace_back(std::move(only));
        return out;
    }

    /// Returns true when the next wire must be a FULL snapshot (delta limit or heartbeat window).
    bool forceSnapshot_(uint64_t sim_time) const
    {
        return distance_to_snapshot_ + 1 >= heartbeat_ || shouldHeartbeatRefresh_(sim_time);
    }

    /// Returns true when the last FULL wire is outside the current heartbeat window.
    bool shouldHeartbeatRefresh_(uint64_t sim_time) const
    {
        if (!last_snapshot_sim_time_.isValid())
        {
            return false;
        }
        if (sim_time <= last_snapshot_sim_time_.getValue())
        {
            return false;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_snapshot_sim_time_.getValue() < window_lo;
    }

    /// Returns true when a CID that skipped this window still needs a heartbeat refresh on the wire.
    bool shouldRefreshAbsentCid_(uint64_t sim_time, bool tip_closed) const
    {
        if (shouldHeartbeatRefresh_(sim_time))
        {
            return true;
        }
        if (!tip_closed || !last_record_closed_sim_time_.isValid() ||
            sim_time <= last_record_closed_sim_time_.getValue())
        {
            return false;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_record_closed_sim_time_.getValue() < window_lo;
    }

    /// Returns true when a CLOSED wire should be preceded by a priming FULL snapshot.
    bool needsClosedPriming_(uint64_t sim_time) const
    {
        if (!last_snapshot_sim_time_.isValid())
        {
            return true;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_snapshot_sim_time_.getValue() < window_lo;
    }

    /// Updates delta-chain and heartbeat bookkeeping after a wire is emitted.
    void recordCidSent_(Action action, uint64_t sim_time)
    {
        if (action == Action::FULL)
        {
            distance_to_snapshot_ = 0;
        } else
        {
            ++distance_to_snapshot_;
        }

        if (action == Action::FULL)
        {
            last_snapshot_sim_time_ = sim_time;
        } else if (action == Action::CLOSED)
        {
            last_record_closed_sim_time_ = sim_time;
        }
    }

    const size_t heartbeat_;
    size_t distance_to_snapshot_ = 0;
    ValidValue<uint64_t> last_snapshot_sim_time_;
    ValidValue<uint64_t> last_record_closed_sim_time_;
};

//! \class ScalarCheckpointer
class ScalarCheckpointer final : public CollectableCheckpointer
{
public:
    /// Constructs a scalar checkpointer with the given heartbeat interval.
    ScalarCheckpointer(size_t heartbeat) :
        CollectableCheckpointer(heartbeat)
    {
    }

    /// Appends a scalar data checkpoint for \p window_id.
    void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes) override
    {
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, scalar_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint; // un-hide the other two

    /// Appends a scalar close lifecycle checkpoint for \p window_id.
    void closeRecord(uint64_t window_id) override
    {
        constexpr bool closed = true;
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, closed);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    /// Returns true if this scalar collectable staged data in \p window_id.
    bool participatedInWindow(uint64_t window_id) const override { return participatedInWindow_(window_id, tail_); }

    /// Encodes scalar wire records for \p cid at \p sim_time.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = getAnchorForWindow_(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_closed = tip && tip->isClosedEvent();
        if (shouldRefreshAbsentCid_(sim_time, tip_closed))
        {
            if (tip_closed)
            {
                return emitRecordClosed_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordCidSent_(Action::FULL, sim_time);
            return makeSingleElemVector_(std::move(encoded));
        }

        return {};
    }

private:
    /// Returns the newest scalar data checkpoint, skipping lifecycle-only nodes.
    ScalarCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Emits CLOSED, optionally priming with FULL when the heartbeat window requires it.
    std::vector<std::unique_ptr<CollectedData>> emitRecordClosed_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsClosedPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordCidSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        recordCidSent_(Action::CLOSED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    /// Encodes the scalar checkpoint at \p anchor and trims the chain through that node.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(ScalarCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = readEncodedAction_(*encoded);
        if (action == Action::CLOSED)
        {
            cleanupThrough_(anchor, action);
            return emitRecordClosed_(cid, sim_time);
        }

        recordCidSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return makeSingleElemVector_(std::move(encoded));
    }

    /// Advances \p tail_ to \p anchor and detaches earlier nodes on FULL.
    void cleanupThrough_(ScalarCheckpoint* anchor, Action action)
    {
        auto sp = getSharedCheckpoint_(anchor, head_);
        if (!sp)
        {
            return;
        }
        if (action == Action::FULL)
        {
            sp->detachPrev();
        }
        tail_ = sp;
    }

    std::shared_ptr<ScalarCheckpoint> head_;
    std::shared_ptr<ScalarCheckpoint> tail_;
};

//! \class ContigContainerCheckpointer
class ContigContainerCheckpointer final : public CollectableCheckpointer
{
public:
    /// Constructs a contig checkpointer with heartbeat and fixed capacity.
    ContigContainerCheckpointer(size_t heartbeat, size_t capacity) :
        CollectableCheckpointer(heartbeat),
        capacity_(capacity)
    {
    }

    /// Appends a contig container data checkpoint for \p window_id.
    void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes) override
    {
        auto size = detail::getContainerSize(contig_bin_bytes);
        assert(size <= capacity_);
        (void)capacity_;
        max_container_size_ = std::max(max_container_size_, size);

        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, contig_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint; // un-hide the other two

    /// Appends a contig close lifecycle checkpoint for \p window_id.
    void closeRecord(uint64_t window_id) override
    {
        constexpr bool closed = true;
        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, closed);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    /// Returns true if this contig collectable staged data in \p window_id.
    bool participatedInWindow(uint64_t window_id) const override { return participatedInWindow_(window_id, tail_); }

    /// Encodes contig container wire records for \p cid at \p sim_time.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = getAnchorForWindow_(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_closed = tip && tip->isClosedEvent();
        if (shouldRefreshAbsentCid_(sim_time, tip_closed))
        {
            if (tip_closed)
            {
                return emitRecordClosed_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordCidSent_(Action::FULL, sim_time);
            return makeSingleElemVector_(std::move(encoded));
        }

        return {};
    }

    /// Writes observed max queue occupancy to QueueMaxSizes.
    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(SQL_TABLE("QueueMaxSizes"), SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    /// Returns the newest contig data checkpoint, skipping lifecycle-only nodes.
    ContigContainerCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Emits CLOSED, optionally priming with FULL when the heartbeat window requires it.
    std::vector<std::unique_ptr<CollectedData>> emitRecordClosed_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsClosedPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordCidSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        recordCidSent_(Action::CLOSED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    /// Encodes the contig checkpoint at \p anchor and trims the chain through that node.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(ContigContainerCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = readEncodedAction_(*encoded);
        if (action == Action::CLOSED)
        {
            cleanupThrough_(anchor, action);
            return emitRecordClosed_(cid, sim_time);
        }

        recordCidSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return makeSingleElemVector_(std::move(encoded));
    }

    /// Advances \p tail_ to \p anchor and detaches earlier nodes on FULL.
    void cleanupThrough_(ContigContainerCheckpoint* anchor, Action action)
    {
        auto sp = getSharedCheckpoint_(anchor, head_);
        if (!sp)
        {
            return;
        }
        if (action == Action::FULL)
        {
            sp->detachPrev();
        }
        tail_ = sp;
    }

    std::shared_ptr<ContigContainerCheckpoint> head_;
    std::shared_ptr<ContigContainerCheckpoint> tail_;
    uint16_t max_container_size_ = 0;
    const uint16_t capacity_;
};

//! \class SparseContainerCheckpointer
class SparseContainerCheckpointer final : public CollectableCheckpointer
{
public:
    /// Constructs a sparse checkpointer with heartbeat and fixed capacity.
    SparseContainerCheckpointer(size_t heartbeat, size_t capacity) :
        CollectableCheckpointer(heartbeat),
        capacity_(capacity)
    {
    }

    /// Appends a sparse container data checkpoint for \p window_id.
    void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes) override
    {
        auto size = detail::getContainerSize(sparse_bin_bytes);
        assert(size <= capacity_);
        (void)capacity_;
        max_container_size_ = std::max(max_container_size_, size);

        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, sparse_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint; // un-hide the other two

    /// Appends a sparse close lifecycle checkpoint for \p window_id.
    void closeRecord(uint64_t window_id) override
    {
        constexpr bool closed = true;
        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, closed);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    /// Returns true if this sparse collectable staged data in \p window_id.
    bool participatedInWindow(uint64_t window_id) const override { return participatedInWindow_(window_id, tail_); }

    /// Encodes sparse container wire records for \p cid at \p sim_time.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = getAnchorForWindow_(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_closed = tip && tip->isClosedEvent();
        if (shouldRefreshAbsentCid_(sim_time, tip_closed))
        {
            if (tip_closed)
            {
                return emitRecordClosed_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordCidSent_(Action::FULL, sim_time);
            return makeSingleElemVector_(std::move(encoded));
        }

        return {};
    }

    /// Writes observed max queue occupancy to QueueMaxSizes.
    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(SQL_TABLE("QueueMaxSizes"), SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    /// Returns the newest sparse data checkpoint, skipping lifecycle-only nodes.
    SparseContainerCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    /// Emits CLOSED, optionally priming with FULL when the heartbeat window requires it.
    std::vector<std::unique_ptr<CollectedData>> emitRecordClosed_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsClosedPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordCidSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::CLOSED);
        recordCidSent_(Action::CLOSED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    /// Encodes the sparse checkpoint at \p anchor and trims the chain through that node.
    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(SparseContainerCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = readEncodedAction_(*encoded);
        if (action == Action::CLOSED)
        {
            cleanupThrough_(anchor, action);
            return emitRecordClosed_(cid, sim_time);
        }

        recordCidSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return makeSingleElemVector_(std::move(encoded));
    }

    /// Advances \p tail_ to \p anchor and detaches earlier nodes on FULL.
    void cleanupThrough_(SparseContainerCheckpoint* anchor, Action action)
    {
        auto sp = getSharedCheckpoint_(anchor, head_);
        if (!sp)
        {
            return;
        }
        if (action == Action::FULL)
        {
            sp->detachPrev();
        }
        tail_ = sp;
    }

    std::shared_ptr<SparseContainerCheckpoint> head_;
    std::shared_ptr<SparseContainerCheckpoint> tail_;
    uint16_t max_container_size_ = 0;
    const uint16_t capacity_;
};

} // namespace simdb::argos
