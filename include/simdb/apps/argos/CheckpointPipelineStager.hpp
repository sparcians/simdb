// <CheckpointPipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace simdb::argos {

//! \class CheckpointPipelineStager
//! \brief Checkpoint-based collection stager.
//!
//! Waiting-queue slots store checkpoints only. sendToPipeline_ derives wire records,
//! refresh eligibility, and heartbeat inject from those checkpoints when flushing
//! slots in order. Send-time bookkeeping is updated only during flush, never at enqueue.
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

        (void)notif_head_;
        (void)dyn_field_head_;
    }

    size_t getHeartbeat() const { return heartbeat_; }

    const std::vector<QueueCollectionData>& getWaitingQueue() const { return waiting_queue_; }

    void clearWaitingQueue() { waiting_queue_.clear(); }

    void setScalarType(uint16_t cid) { scalar_cids_.insert(cid); }

    void setContainerType(uint16_t cid, bool sparse, size_t capacity)
    {
        (void)capacity;
        if (sparse)
        {
            sparse_cids_.insert(cid);
        } else
        {
            container_cids_.insert(cid);
        }
    }

    void stage(uint16_t cid, const std::vector<char>& scalar_bytes)
    {
        assert(scalar_cids_.count(cid) > 0);
        advanceSimTimeSlot();
        auto& checkpointer = getOrCreateScalarCheckpointer_(cid);
        waiting_queue_.back().checkpoints[cid] = checkpointer.createCheckpoint(scalar_bytes);
    }

    void stage(uint16_t cid, const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        assert(container_cids_.count(cid) > 0);
        advanceSimTimeSlot();
        auto& checkpointer = getOrCreateContigCheckpointer_(cid);
        waiting_queue_.back().checkpoints[cid] = checkpointer.createCheckpoint(contig_bin_bytes);
    }

    void stage(uint16_t cid, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        assert(sparse_cids_.count(cid) > 0);
        advanceSimTimeSlot();
        auto& checkpointer = getOrCreateSparseCheckpointer_(cid);
        waiting_queue_.back().checkpoints[cid] = checkpointer.createCheckpoint(sparse_bin_bytes);
    }

    void onEnabledChanged(uint16_t cid, bool enabled)
    {
        if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                enabled ? checkpointer->createReenabledCheckpoint() : checkpointer->createDisabledCheckpoint();
        } else if (auto* checkpointer = findContigCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                enabled ? checkpointer->createReenabledCheckpoint() : checkpointer->createDisabledCheckpoint();
        } else if (auto* checkpointer = findSparseCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                enabled ? checkpointer->createReenabledCheckpoint() : checkpointer->createDisabledCheckpoint();
        }
    }

    void onQuietChanged(uint16_t cid, bool quiet)
    {
        if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                quiet ? checkpointer->createQuietedCheckpoint() : checkpointer->createReenabledCheckpoint();
        } else if (auto* checkpointer = findContigCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                quiet ? checkpointer->createQuietedCheckpoint() : checkpointer->createReenabledCheckpoint();
        } else if (auto* checkpointer = findSparseCheckpointer_(cid); checkpointer && checkpointer->tip())
        {
            advanceSimTimeSlot();
            waiting_queue_.back().checkpoints[cid] =
                quiet ? checkpointer->createQuietedCheckpoint() : checkpointer->createReenabledCheckpoint();
        }
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

    void advanceSimTimeSlot()
    {
        if (advanceStageTime_())
        {
            QueueCollectionData entry;
            entry.sim_time = last_stage_time_.getValue();
            waiting_queue_.push_back(std::move(entry));
        }
    }

    //! Tip of the per-CID checkpoint chain after the latest flush/enqueue activity.
    std::shared_ptr<const Checkpoint> getTip(uint16_t cid) const
    {
        if (auto it = scalar_checkpointers_.find(cid); it != scalar_checkpointers_.end())
        {
            return it->second->tip();
        }
        if (auto it = contig_checkpointers_.find(cid); it != contig_checkpointers_.end())
        {
            return it->second->tip();
        }
        if (auto it = sparse_checkpointers_.find(cid); it != sparse_checkpointers_.end())
        {
            return it->second->tip();
        }
        return nullptr;
    }

private:
    static bool isLifecycleAction_(const Checkpoint& checkpoint)
    {
        auto action = checkpoint.getAction();
        return static_cast<uint8_t>(action) < static_cast<uint8_t>(Action::FULL);
    }

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

    ContigCheckpointer& getOrCreateContigCheckpointer_(uint16_t cid)
    {
        auto it = contig_checkpointers_.find(cid);
        if (it == contig_checkpointers_.end())
        {
            it = contig_checkpointers_.emplace(cid, std::make_unique<ContigCheckpointer>(cid, heartbeat_)).first;
        }
        return *it->second;
    }

    ContigCheckpointer* findContigCheckpointer_(uint16_t cid)
    {
        auto it = contig_checkpointers_.find(cid);
        if (it == contig_checkpointers_.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    SparseCheckpointer& getOrCreateSparseCheckpointer_(uint16_t cid)
    {
        auto it = sparse_checkpointers_.find(cid);
        if (it == sparse_checkpointers_.end())
        {
            it = sparse_checkpointers_.emplace(cid, std::make_unique<SparseCheckpointer>(cid, heartbeat_)).first;
        }
        return *it->second;
    }

    SparseCheckpointer* findSparseCheckpointer_(uint16_t cid)
    {
        auto it = sparse_checkpointers_.find(cid);
        if (it == sparse_checkpointers_.end())
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
        QueueCollectionData to_send;
        to_send.sim_time = collection_at_time.sim_time;

        std::unordered_set<uint16_t> handled_cids;
        for (const auto& [cid, checkpoint] : collection_at_time.checkpoints)
        {
            // First case: the checkpoint represents a FULL snapshot.
            // Dump it as-is and remove the parent checkpoint chain
            // so we don't consume memory forever.
            //
            // Note that this applies to just-enabled / just-awakened
            // collectables too.
            if (checkpoint->isSnapshot())
            {
                auto full_data = checkpoint->getFullData();
                to_send.entries.emplace_back(std::move(full_data));
                checkpoint->detachFromParent();
                handled_cids.insert(cid);
            }

            // Second case: not a snapshot, and not a lifecycle event.
            // Just a regular delta inside the heartbeat interval.
            else if (!isLifecycleAction_(*checkpoint))
            {
                auto delta_data = checkpoint->getMinifiedData();
                to_send.entries.emplace_back(std::move(delta_data));
                handled_cids.insert(cid);
            }

            // Third case: we are disabling/quieting a collectable.
            // Emit action-only wire bytes; tip stays VanishedCheckpoint.
            else if (isVanishingLifecycle_(*checkpoint))
            {
                auto mini_data = checkpoint->getMinifiedData();
                to_send.entries.emplace_back(std::move(mini_data));
                handled_cids.insert(cid);
            }
        }

        // Replay FULL bytes for refreshable CIDs not handled in this flush.
        for (const auto& [cid, checkpointer] : scalar_checkpointers_)
        {
            if (!handled_cids.insert(cid).second)
            {
                continue;
            }

            if (!checkpointer->isRefreshable())
            {
                continue;
            }

            if (checkpointer->isDueForWireRefresh())
            {
                auto full_data = checkpointer->tip()->getFullData();
                to_send.entries.emplace_back(std::move(full_data));
                checkpointer->rebaseTipAfterWireFull(*to_send.entries.back());
            } else
            {
                checkpointer->recordMissedFlush();
            }
        }

        for (const auto& [cid, checkpointer] : contig_checkpointers_)
        {
            if (!handled_cids.insert(cid).second)
            {
                continue;
            }

            if (!checkpointer->isRefreshable())
            {
                continue;
            }

            if (checkpointer->isDueForWireRefresh())
            {
                auto full_data = checkpointer->tip()->getFullData();
                to_send.entries.emplace_back(std::move(full_data));
                checkpointer->rebaseTipAfterWireFull(*to_send.entries.back());
            } else
            {
                checkpointer->recordMissedFlush();
            }
        }

        for (const auto& [cid, checkpointer] : sparse_checkpointers_)
        {
            if (!handled_cids.insert(cid).second)
            {
                continue;
            }

            if (!checkpointer->isRefreshable())
            {
                continue;
            }

            if (checkpointer->isDueForWireRefresh())
            {
                auto full_data = checkpointer->tip()->getFullData();
                to_send.entries.emplace_back(std::move(full_data));
                checkpointer->rebaseTipAfterWireFull(*to_send.entries.back());
            } else
            {
                checkpointer->recordMissedFlush();
            }
        }

        if (!to_send.entries.empty() && pipeline_head_ != nullptr)
        {
            pipeline_head_->emplace(std::move(to_send));
        }
    }

    static bool isVanishingLifecycle_(const Checkpoint& checkpoint)
    {
        auto action = checkpoint.getAction();
        return action == Action::DISABLED || action == Action::QUIETED;
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
    std::unordered_set<uint16_t> container_cids_;
    std::unordered_set<uint16_t> sparse_cids_;
    std::unordered_map<uint16_t, std::unique_ptr<ScalarCheckpointer>> scalar_checkpointers_;
    std::unordered_map<uint16_t, std::unique_ptr<ContigCheckpointer>> contig_checkpointers_;
    std::unordered_map<uint16_t, std::unique_ptr<SparseCheckpointer>> sparse_checkpointers_;
};

} // namespace simdb::argos
