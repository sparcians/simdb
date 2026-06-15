// <CheckpointerAsyncEncode.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/Checkpoint.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"

#include <memory>
#include <vector>

namespace simdb::argos {

//! Per-CID wire accounting used by the async encoder stage (mirrors CollectableCheckpointer policy).
class WireAccountingState
{
public:
    void ensureHeartbeat(size_t heartbeat)
    {
        if (heartbeat_ == 0)
        {
            heartbeat_ = heartbeat;
        }
    }

    bool forceSnapshot(uint64_t sim_time) const
    {
        return wire_distance_ + 1 >= heartbeat_ || shouldHeartbeatRefresh(sim_time);
    }

    bool shouldAbsentHeartbeatRefresh(uint64_t sim_time, bool tip_disabled) const
    {
        if (shouldHeartbeatRefresh(sim_time))
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

    bool needsDisabledPriming(uint64_t sim_time) const
    {
        if (!last_full_wired_sim_time_.isValid())
        {
            return true;
        }
        const uint64_t window_lo = sim_time >= heartbeat_ ? sim_time - heartbeat_ + 1 : 0;
        return last_full_wired_sim_time_.getValue() < window_lo;
    }

    void recordWireSent(Action action, uint64_t sim_time)
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

private:
    bool shouldHeartbeatRefresh(uint64_t sim_time) const
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

    size_t heartbeat_ = 0;
    size_t wire_distance_ = 0;
    ValidValue<uint64_t> last_full_wired_sim_time_;
    ValidValue<uint64_t> last_disabled_wired_sim_time_;
};

namespace detail {

template <typename CheckpointT>
inline CheckpointT* latestDataCheckpoint(CheckpointT* head)
{
    auto* checkpoint = head;
    while (checkpoint && !checkpoint->isDataCheckpoint())
    {
        checkpoint = checkpoint->prev();
    }
    return checkpoint;
}

template <typename CheckpointT>
inline void cleanupSliceThrough(CheckpointT* anchor, Action action)
{
    if (!anchor)
    {
        return;
    }
    if (action == Action::FULL)
    {
        anchor->detachPrev();
    }
}

template <typename CheckpointT>
inline std::vector<std::unique_ptr<CollectedData>> emitDisabledWires(CheckpointT* head, uint16_t cid,
                                                                      uint64_t sim_time,
                                                                      WireAccountingState& wire_state)
{
    std::vector<std::unique_ptr<CollectedData>> out;
    if (wire_state.needsDisabledPriming(sim_time))
    {
        if (auto* latest = latestDataCheckpoint(head))
        {
            out.push_back(latest->encodeSnapshotForPipeline(cid));
            wire_state.recordWireSent(Action::FULL, sim_time);
        }
    }

    auto encoded = std::make_unique<CollectedData>(cid);
    encoded->getBuffer().append(Action::DISABLED);
    wire_state.recordWireSent(Action::DISABLED, sim_time);
    out.push_back(std::move(encoded));
    return out;
}

template <typename CheckpointT>
inline std::vector<std::unique_ptr<CollectedData>> encodeActive(CheckpointT* anchor, uint16_t cid, bool force_snapshot,
                                                                 uint64_t sim_time, WireAccountingState& wire_state)
{
    auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
    if (!encoded)
    {
        return {};
    }

    const auto action = readEncodedAction(*encoded);
    if (action == Action::DISABLED)
    {
        cleanupSliceThrough(anchor, action);
        return emitDisabledWires(anchor, cid, sim_time, wire_state);
    }

    wire_state.recordWireSent(action, sim_time);
    cleanupSliceThrough(anchor, action);
    std::vector<std::unique_ptr<CollectedData>> out;
    out.push_back(std::move(encoded));
    return out;
}

template <typename CheckpointT>
inline std::vector<std::unique_ptr<CollectedData>> encodeContainerWork(const CidEncodeWork& work, uint64_t sim_time,
                                                                       WireAccountingState& wire_state)
{
    auto* head = static_cast<CheckpointT*>(work.slice_head.get());
    if (work.path == EncodePath::ActiveAnchor)
    {
        auto* anchor = static_cast<CheckpointT*>(work.anchor);
        return encodeActive(anchor, work.cid, wire_state.forceSnapshot(sim_time), sim_time, wire_state);
    }

    if (!wire_state.shouldAbsentHeartbeatRefresh(sim_time, work.tip_disabled))
    {
        return {};
    }

    if (work.tip_disabled)
    {
        return emitDisabledWires(head, work.cid, sim_time, wire_state);
    }

    auto* latest = latestDataCheckpoint(head);
    if (!latest)
    {
        return {};
    }

    auto encoded = latest->encodeSnapshotForPipeline(work.cid);
    wire_state.recordWireSent(Action::FULL, sim_time);
    std::vector<std::unique_ptr<CollectedData>> out;
    out.push_back(std::move(encoded));
    return out;
}

inline std::vector<std::unique_ptr<CollectedData>> encodeScalarWork(const CidEncodeWork& work, uint64_t sim_time,
                                                                    WireAccountingState& wire_state)
{
    auto* head = static_cast<ScalarCheckpoint*>(work.slice_head.get());
    if (work.path == EncodePath::ActiveAnchor)
    {
        auto* anchor = static_cast<ScalarCheckpoint*>(work.anchor);
        return encodeActive(anchor, work.cid, wire_state.forceSnapshot(sim_time), sim_time, wire_state);
    }

    if (!wire_state.shouldAbsentHeartbeatRefresh(sim_time, work.tip_disabled))
    {
        return {};
    }

    if (work.tip_disabled)
    {
        return emitDisabledWires(head, work.cid, sim_time, wire_state);
    }

    auto* latest = latestDataCheckpoint(head);
    if (!latest)
    {
        return {};
    }

    auto encoded = latest->encodeSnapshotForPipeline(work.cid);
    wire_state.recordWireSent(Action::FULL, sim_time);
    std::vector<std::unique_ptr<CollectedData>> out;
    out.push_back(std::move(encoded));
    return out;
}

} // namespace detail

inline std::vector<std::unique_ptr<CollectedData>> encodeCidWork(const CidEncodeWork& work, uint64_t sim_time,
                                                                 WireAccountingState& wire_state)
{
    wire_state.ensureHeartbeat(work.heartbeat);
    switch (work.kind)
    {
    case CheckpointerKind::Scalar:
        return detail::encodeScalarWork(work, sim_time, wire_state);
    case CheckpointerKind::Contig:
        return detail::encodeContainerWork<ContigContainerCheckpoint>(work, sim_time, wire_state);
    case CheckpointerKind::Sparse:
        return detail::encodeContainerWork<SparseContainerCheckpoint>(work, sim_time, wire_state);
    }
    throw DBException("Invalid CheckpointerKind");
}

} // namespace simdb::argos
