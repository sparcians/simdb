// <CheckpointPipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace simdb::argos {

//! \class CheckpointPipelineStager
//! \brief Checkpoint-based collection stager.
//!
//! Phase A: enqueue-only. Each simulation time slot stores checkpoints; send logic
//! is rebuilt in a follow-up PR. Internal state is intentionally minimal.
class CheckpointPipelineStager
{
public:
    CheckpointPipelineStager(size_t heartbeat, Timestamp* timestamp,
                             ConcurrentQueue<QueueCollectionData>* pipeline_head,
                             ConcurrentQueue<Notification>* notif_head = nullptr,
                             ConcurrentQueue<DynamicFieldChanges>* dyn_field_head = nullptr) :
        heartbeat_(heartbeat),
        timestamp_(timestamp),
        pipeline_head_(pipeline_head),
        notif_head_(notif_head),
        dyn_field_head_(dyn_field_head)
    {
        assert(heartbeat > 0);
        assert(timestamp != nullptr);
    }

    size_t getHeartbeat() const { return heartbeat_; }

    const std::vector<QueueCollectionData>& getWaitingQueue() const { return waiting_queue_; }

    void clearWaitingQueue() { waiting_queue_.clear(); }

    void setScalarType(uint16_t cid) { scalar_cids_.insert(cid); }

    void stage(uint16_t cid, const std::vector<char>& scalar_bytes)
    {
        assert(scalar_cids_.count(cid) > 0);

        advanceSimTimeSlot();

        auto& checkpointer = getOrCreateScalarCheckpointer_(cid);
        waiting_queue_.back().checkpoints[cid] = checkpointer.createCheckpoint(scalar_bytes);
    }

    void sendCollectedDataToPipeline()
    {
        while (!waiting_queue_.empty())
        {
            sendToPipeline_(waiting_queue_.front());
            waiting_queue_.erase(waiting_queue_.begin());
        }
    }

    void disableAutoSendMode(bool disable = true) { auto_send_when_time_advances_ = !disable; }

    //! Create or extend the waiting-queue slot for the current simulation time without
    //! recording collection data (used when a tick has lifecycle-only activity).
    void advanceSimTimeSlot()
    {
        if (advanceStageTime_())
        {
            QueueCollectionData entry;
            entry.sim_time = last_stage_time_.getValue();
            waiting_queue_.push_back(std::move(entry));
        }
    }

    void onEnabledChanged(uint16_t cid, bool enabled)
    {
        advanceSimTimeSlot();

        if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            waiting_queue_.back().checkpoints[cid] =
                enabled ? checkpointer->createReenabledCheckpoint() : checkpointer->createDisabledCheckpoint();
        }
    }

    void onQuietChanged(uint16_t cid, bool quiet)
    {
        advanceSimTimeSlot();

        if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            waiting_queue_.back().checkpoints[cid] =
                quiet ? checkpointer->createQuietedCheckpoint() : checkpointer->createReenabledCheckpoint();
        }
    }

private:
    ScalarCheckpointer& getOrCreateScalarCheckpointer_(uint16_t cid)
    {
        auto it = scalar_checkpointers_.find(cid);
        if (it == scalar_checkpointers_.end())
        {
            it = scalar_checkpointers_.emplace(cid, std::make_unique<ScalarCheckpointer>(cid, heartbeat_)).first;
        }
        return *it->second;
    }

    ScalarCheckpointer* findScalarCheckpointer_(uint16_t cid)
    {
        auto it = scalar_checkpointers_.find(cid);
        if (it == scalar_checkpointers_.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    bool advanceStageTime_()
    {
        const uint64_t current_time = timestamp_->getTime();
        if (!last_stage_time_.isValid() || current_time >= last_stage_time_.getValue())
        {
            last_stage_time_ = current_time;
        } else
        {
            throw DBException("Time must be monotonically increasing");
        }

        if (waiting_queue_.empty())
        {
            return true;
        }

        const auto prev_slot_time = waiting_queue_.back().sim_time;
        if (current_time == prev_slot_time)
        {
            return false;
        }
        if (current_time < prev_slot_time)
        {
            throw DBException("Time must be monotonically increasing");
        }

        if (auto_send_when_time_advances_)
        {
            sendCollectedDataToPipeline();
        }

        return true;
    }

    void sendToPipeline_(const QueueCollectionData& collection_at_time)
    {
        // Phase B: derive wire records, heartbeat inject, and pipeline enqueue from checkpoints.
        (void)collection_at_time;
        (void)pipeline_head_;
    }

    const size_t heartbeat_;
    Timestamp* const timestamp_;
    ConcurrentQueue<QueueCollectionData>* const pipeline_head_;
    ConcurrentQueue<Notification>* const notif_head_;
    ConcurrentQueue<DynamicFieldChanges>* const dyn_field_head_;

    std::vector<QueueCollectionData> waiting_queue_;
    ValidValue<uint64_t> last_stage_time_;
    bool auto_send_when_time_advances_ = true;

    std::unordered_set<uint16_t> scalar_cids_;
    std::unordered_map<uint16_t, std::unique_ptr<ScalarCheckpointer>> scalar_checkpointers_;
};

} // namespace simdb::argos
