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

struct EncodeCidResult
{
    std::vector<std::unique_ptr<CollectedData>> wires;
    bool release_through_full_anchor = false;
    bool committed_active_anchor = false;
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
inline EncodeCidResult emitDisabledWires(CheckpointT* head, uint16_t cid, uint64_t sim_time,
                                         WireAccountingState& wire_state)
{
    EncodeCidResult result;
    if (wire_state.needsDisabledPriming(sim_time))
    {
        if (auto* latest = latestDataCheckpoint(head))
        {
            result.wires.push_back(latest->encodeSnapshotForPipeline(cid));
            wire_state.recordWireSent(Action::FULL, sim_time);
        }
    }

    auto encoded = std::make_unique<CollectedData>(cid);
    encoded->getBuffer().append(Action::DISABLED);
    wire_state.recordWireSent(Action::DISABLED, sim_time);
    result.wires.push_back(std::move(encoded));
    return result;
}

template <typename CheckpointT>
inline EncodeCidResult encodeActive(CheckpointT* anchor, uint16_t cid, bool force_snapshot, uint64_t sim_time,
                                    WireAccountingState& wire_state)
{
    auto encoded = anchor->encodeForPipeline(cid, force_snapshot);
    if (!encoded)
    {
        return {};
    }

    const auto action = readEncodedAction(*encoded);
    if (action == Action::DISABLED)
    {
        EncodeCidResult disabled = emitDisabledWires(anchor, cid, sim_time, wire_state);
        disabled.committed_active_anchor = true;
        return disabled;
    }

    wire_state.recordWireSent(action, sim_time);
    EncodeCidResult result;
    result.committed_active_anchor = true;
    result.release_through_full_anchor = (action == Action::FULL);
    result.wires.push_back(std::move(encoded));
    return result;
}

template <typename CheckpointT>
inline EncodeCidResult encodeContainerWork(const CidEncodeWork& work, uint64_t sim_time,
                                           WireAccountingState& wire_state)
{
    auto* head = static_cast<CheckpointT*>(work.stolen_chain_tail.get());
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
    EncodeCidResult result;
    result.wires.push_back(std::move(encoded));
    return result;
}

inline EncodeCidResult encodeScalarWork(const CidEncodeWork& work, uint64_t sim_time, WireAccountingState& wire_state)
{
    auto* head = static_cast<ScalarCheckpoint*>(work.stolen_chain_tail.get());
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
    EncodeCidResult result;
    result.wires.push_back(std::move(encoded));
    return result;
}

} // namespace detail

inline EncodeCidResult encodeCidWork(const CidEncodeWork& work, uint64_t sim_time, WireAccountingState& wire_state)
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
