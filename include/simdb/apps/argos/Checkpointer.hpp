// <Checkpointer.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpoint.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <vector>

namespace simdb::argos {

namespace detail {

template <typename CheckpointT>
CheckpointT* getAnchorForWindow(uint64_t window_id, const std::shared_ptr<CheckpointT>& tail)
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

template <typename CheckpointT>
inline bool participatedInWindow(uint64_t window_id, const std::shared_ptr<CheckpointT>& tail)
{
    if (!tail)
    {
        return false;
    }
    auto* anchor = getAnchorForWindow(window_id, tail);
    return anchor && anchor->getWindowID() == window_id;
}

template <typename CheckpointT>
std::shared_ptr<CheckpointT> getSharedCheckpoint(CheckpointT* raw, const std::shared_ptr<CheckpointT>& head)
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

inline void recordWireSent(Action action, size_t& wire_distance)
{
    if (action == Action::FULL || action == Action::ENABLED)
    {
        wire_distance = 0;
    } else
    {
        ++wire_distance;
    }
}

inline Action readEncodedAction(const CollectedData& encoded)
{
    return static_cast<Action>(encoded.getData()[sizeof(uint16_t)]);
}

inline std::vector<std::unique_ptr<CollectedData>> singleWire(std::unique_ptr<CollectedData> wire)
{
    std::vector<std::unique_ptr<CollectedData>> out;
    out.push_back(std::move(wire));
    return out;
}

} // namespace detail

//! \class CollectableCheckpointer
//! \brief Base class for all collectable checkpointers.
class CollectableCheckpointer
{
public:
    virtual ~CollectableCheckpointer() = default;

    virtual void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes)
    {
        (void)window_id;
        (void)scalar_bytes;
        throw DBException("Not implemented");
    }

    virtual void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        (void)window_id;
        (void)contig_bin_bytes;
        throw DBException("Not implemented");
    }

    virtual void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        (void)window_id;
        (void)sparse_bin_bytes;
        throw DBException("Not implemented");
    }

    virtual void onEnabledChanged(uint64_t window_id, bool enabled) = 0;

    virtual void onQuietChanged(uint64_t window_id, bool quiet) = 0;

    virtual std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                          uint16_t cid) = 0;

    virtual bool participatedInWindow(uint64_t window_id) const = 0;

    virtual void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager*) { (void)cid; }

protected:
    explicit CollectableCheckpointer(size_t heartbeat) :
        heartbeat_(heartbeat)
    {
    }

    bool forceSnapshot_(uint64_t sim_time) const
    {
        return wire_distance_ + 1 >= heartbeat_ || shouldHeartbeatRefresh_(sim_time);
    }

    bool shouldHeartbeatRefresh_(uint64_t sim_time) const
    {
        if (!last_full_wired_sim_time_.isValid())
        {
            return false;
        }
        if (sim_time <= last_full_wired_sim_time_.getValue())
        {
            return false;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_full_wired_sim_time_.getValue() < window_lo;
    }

    bool shouldAbsentHeartbeatRefresh_(uint64_t sim_time, bool tip_disabled) const
    {
        if (shouldHeartbeatRefresh_(sim_time))
        {
            return true;
        }
        if (!tip_disabled || !last_disabled_wired_sim_time_.isValid() ||
            sim_time <= last_disabled_wired_sim_time_.getValue())
        {
            return false;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_disabled_wired_sim_time_.getValue() < window_lo;
    }

    bool needsDisabledPriming_(uint64_t sim_time) const
    {
        if (!last_full_wired_sim_time_.isValid())
        {
            return true;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_full_wired_sim_time_.getValue() < window_lo;
    }

    void recordWireSent_(Action action, uint64_t sim_time)
    {
        detail::recordWireSent(action, wire_distance_);
        if (action == Action::FULL || action == Action::ENABLED)
        {
            last_full_wired_sim_time_ = sim_time;
        } else if (action == Action::DISABLED)
        {
            last_disabled_wired_sim_time_ = sim_time;
        }
    }

    const size_t heartbeat_;
    size_t wire_distance_ = 0;
    ValidValue<uint64_t> last_full_wired_sim_time_;
    ValidValue<uint64_t> last_disabled_wired_sim_time_;
};

//! \class ScalarCheckpointer
//! \brief Responsible for delta encoding for scalar types (including structs)
class ScalarCheckpointer final : public CollectableCheckpointer
{
public:
    ScalarCheckpointer(size_t heartbeat) :
        CollectableCheckpointer(heartbeat)
    {
    }

    void createCheckpoint(uint64_t window_id, const std::vector<char>& scalar_bytes) override
    {
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, scalar_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint;  // un-hide the other two

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<ScalarCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override { onEnabledChanged(window_id, quiet); }

    bool participatedInWindow(uint64_t window_id) const override
    {
        return detail::participatedInWindow(window_id, tail_);
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = detail::getAnchorForWindow(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_disabled = tip && tip->isDisabledEvent();
        if (shouldAbsentHeartbeatRefresh_(sim_time, tip_disabled))
        {
            if (tip_disabled)
            {
                return emitDisabledWires_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordWireSent_(Action::FULL, sim_time);
            return detail::singleWire(std::move(encoded));
        }

        return {};
    }

private:
    ScalarCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    std::vector<std::unique_ptr<CollectedData>> emitDisabledWires_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsDisabledPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordWireSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::DISABLED);
        recordWireSent_(Action::DISABLED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(ScalarCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = detail::readEncodedAction(*encoded);
        if (action == Action::DISABLED)
        {
            cleanupThrough_(anchor, action);
            return emitDisabledWires_(cid, sim_time);
        }

        recordWireSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return detail::singleWire(std::move(encoded));
    }

    void cleanupThrough_(ScalarCheckpoint* anchor, Action action)
    {
        auto sp = detail::getSharedCheckpoint(anchor, head_);
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
//! \brief Responsible for delta encoding for contiguous containers
class ContigContainerCheckpointer final : public CollectableCheckpointer
{
public:
    ContigContainerCheckpointer(size_t heartbeat, size_t capacity) :
        CollectableCheckpointer(heartbeat),
        capacity_(capacity)
    {
    }

    void createCheckpoint(uint64_t window_id, const std::vector<std::vector<char>>& contig_bin_bytes) override
    {
        max_container_size_ = std::max(max_container_size_, getSize_(contig_bin_bytes));
        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, contig_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint;  // un-hide the other two

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<ContigContainerCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override { onEnabledChanged(window_id, quiet); }

    bool participatedInWindow(uint64_t window_id) const override
    {
        return detail::participatedInWindow(window_id, tail_);
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = detail::getAnchorForWindow(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_disabled = tip && tip->isDisabledEvent();
        if (shouldAbsentHeartbeatRefresh_(sim_time, tip_disabled))
        {
            if (tip_disabled)
            {
                return emitDisabledWires_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordWireSent_(Action::FULL, sim_time);
            return detail::singleWire(std::move(encoded));
        }

        return {};
    }

    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(SQL_TABLE("QueueMaxSizes"), SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    ContigContainerCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    std::vector<std::unique_ptr<CollectedData>> emitDisabledWires_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsDisabledPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordWireSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::DISABLED);
        recordWireSent_(Action::DISABLED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(ContigContainerCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = detail::readEncodedAction(*encoded);
        if (action == Action::DISABLED)
        {
            cleanupThrough_(anchor, action);
            return emitDisabledWires_(cid, sim_time);
        }

        recordWireSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return detail::singleWire(std::move(encoded));
    }

    void cleanupThrough_(ContigContainerCheckpoint* anchor, Action action)
    {
        auto sp = detail::getSharedCheckpoint(anchor, head_);
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

    size_t getSize_(const std::vector<std::vector<char>>& contig_bin_bytes) const
    {
        size_t size = 0;
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
        assert(size <= capacity_);
        (void)capacity_;
        return size;
    }

    std::shared_ptr<ContigContainerCheckpoint> head_;
    std::shared_ptr<ContigContainerCheckpoint> tail_;
    size_t max_container_size_ = 0;
    const size_t capacity_;
};

//! \class SparseContainerCheckpointer
//! \brief Responsible for delta encoding for sparse containers
class SparseContainerCheckpointer final : public CollectableCheckpointer
{
public:
    SparseContainerCheckpointer(size_t heartbeat, size_t capacity) :
        CollectableCheckpointer(heartbeat),
        capacity_(capacity)
    {
    }

    void createCheckpoint(uint64_t window_id, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes) override
    {
        max_container_size_ = std::max(max_container_size_, getSize_(sparse_bin_bytes));
        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, sparse_bin_bytes);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    using CollectableCheckpointer::createCheckpoint;  // un-hide the other two

    void onEnabledChanged(uint64_t window_id, bool enabled) override
    {
        head_ = std::make_shared<SparseContainerCheckpoint>(head_, window_id, enabled);
        if (!tail_)
        {
            tail_ = head_;
        }
    }

    void onQuietChanged(uint64_t window_id, bool quiet) override { onEnabledChanged(window_id, quiet); }

    bool participatedInWindow(uint64_t window_id) const override
    {
        return detail::participatedInWindow(window_id, tail_);
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline(uint64_t window_id, uint64_t sim_time,
                                                                  uint16_t cid) override
    {
        auto* anchor = detail::getAnchorForWindow(window_id, tail_);
        if (anchor && anchor->getWindowID() == window_id)
        {
            return encodeForPipeline_(anchor, cid, forceSnapshot_(sim_time), sim_time);
        }

        auto* tip = head_.get();
        const bool tip_disabled = tip && tip->isDisabledEvent();
        if (shouldAbsentHeartbeatRefresh_(sim_time, tip_disabled))
        {
            if (tip_disabled)
            {
                return emitDisabledWires_(cid, sim_time);
            }

            auto* latest = latestDataCheckpoint_();
            if (!latest)
            {
                return {};
            }

            auto encoded = latest->encodeSnapshotForPipeline(cid);
            recordWireSent_(Action::FULL, sim_time);
            return detail::singleWire(std::move(encoded));
        }

        return {};
    }

    void writeMetaOnPostTeardown(uint16_t cid, DatabaseManager* db_mgr) override
    {
        db_mgr->INSERT(SQL_TABLE("QueueMaxSizes"), SQL_VALUES((int)cid, (int)max_container_size_));
    }

private:
    SparseContainerCheckpoint* latestDataCheckpoint_() const
    {
        auto* checkpoint = head_.get();
        while (checkpoint && !checkpoint->isDataCheckpoint())
        {
            checkpoint = checkpoint->prev();
        }
        return checkpoint;
    }

    std::vector<std::unique_ptr<CollectedData>> emitDisabledWires_(uint16_t cid, uint64_t sim_time)
    {
        std::vector<std::unique_ptr<CollectedData>> out;
        if (needsDisabledPriming_(sim_time))
        {
            if (auto* latest = latestDataCheckpoint_())
            {
                out.push_back(latest->encodeSnapshotForPipeline(cid));
                recordWireSent_(Action::FULL, sim_time);
            }
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        encoded->getBuffer().append(Action::DISABLED);
        recordWireSent_(Action::DISABLED, sim_time);
        out.push_back(std::move(encoded));
        return out;
    }

    std::vector<std::unique_ptr<CollectedData>> encodeForPipeline_(SparseContainerCheckpoint* anchor, uint16_t cid,
                                                                   bool force_snapshot, uint64_t sim_time)
    {
        auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
        if (!encoded)
        {
            return {};
        }

        const auto action = detail::readEncodedAction(*encoded);
        if (action == Action::DISABLED)
        {
            cleanupThrough_(anchor, action);
            return emitDisabledWires_(cid, sim_time);
        }

        recordWireSent_(action, sim_time);
        cleanupThrough_(anchor, action);
        return detail::singleWire(std::move(encoded));
    }

    void cleanupThrough_(SparseContainerCheckpoint* anchor, Action action)
    {
        auto sp = detail::getSharedCheckpoint(anchor, head_);
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

    size_t getSize_(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes) const
    {
        size_t size = 0;
        for (const auto& [_, bin_bytes] : sparse_bin_bytes)
        {
            if (!bin_bytes.empty())
            {
                ++size;
            }
        }
        assert(size <= UINT16_MAX);
        assert(size <= capacity_);
        (void)capacity_;
        return size;
    }

    std::shared_ptr<SparseContainerCheckpoint> head_;
    std::shared_ptr<SparseContainerCheckpoint> tail_;
    size_t max_container_size_ = 0;
    const size_t capacity_;
};

} // namespace simdb::argos
