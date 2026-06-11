// <CheckpointPipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpointer.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace simdb::argos {

//! \class CheckpointPipelineStager
//! \brief Checkpoint-based collection stager.
class CheckpointPipelineStager
{
public:
    CheckpointPipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* pipeline_head,
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
        assert(pipeline_head != nullptr);
    }

    size_t getHeartbeat() const { return heartbeat_; }

    void setScalarType(uint16_t cid) { scalar_cids_.insert(cid); }

    void stage(uint16_t cid, const std::vector<char>& scalar_bytes)
    {
        assert(scalar_cids_.count(cid) > 0);

        enabled_cids_.insert(cid);
        refreshable_cids_.insert(cid);
        advanceSimTimeSlot();

        auto& checkpointer = getOrCreateScalarCheckpointer_(cid);
        auto checkpoint = checkpointer.createCheckpoint(scalar_bytes);
        latest_checkpoints_[cid] = checkpoint;
        waiting_queue_.back().checkpoints[cid] = checkpoint;
    }

    void sendCollectedDataToPipeline()
    {
        while (!waiting_queue_.empty())
        {
            sendToPipeline_(waiting_queue_.front());
            waiting_queue_.pop();
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
            waiting_queue_.emplace(std::move(entry));
        }
    }

    void onEnabledChanged(uint16_t cid, bool enabled)
    {
        advanceSimTimeSlot();

        //TODO XXX:
        // I don't think this logic is correct. In fact, I don't think the
        // final stager design will have any of these:
        //   enabled_cids_
        //   refreshable_cids_
        //   countdowns_to_refresh_
        //   last_sent_bytes_
        //
        // The only thing we can safely do here is to create the appropriate checkpoint.
        // The reason is that we are currently at time T, but the waiting queue could
        // still be starting at time T-500 or something. If we go ahead and touch the
        // enabled_cids_ etc. data structures right now, then things will get confused
        // in sendToPipeline_ while processing T-500, T-499, etc.
        //
        // The name of the game for this design:
        //   *Minimize all internal state. We need to be aggressive at removing unnecessary
        //   data structures, since the logic gets complicated quickly if we try to keep
        //   everything mutually consistent across simulation time points. At the end of
        //   the day, everything should be figured out using checkpoints only in the
        //   sendToPipeline_ method.
        //
        // Can we try the following:
        //   - remove enabled_cids_, refreshable_cids_, and countdowns_to_refresh_ entirely
        //   - remove last_sent_bytes_ entirely
        //   - capture the appropriate checkpoints in enabled/quiet changes
        //   - remove enabled_changes/quiet_changes from the QueueCollectionData
        //   - delete all code in sendToPipeline_ for now since no test uses it / queries the DB to validate
        //
        // The reason we have to do this now is that we are starting to "play favorites" again
        // with the legacy PipelineStager design. Our goal should be to build-up the new
        // design without introducing coupling/complexity across any added data structures.
        // The end of this incremental PR should only use checkpoints for in-memory unit testing
        // using your harness.

        waiting_queue_.back().enabled_changes.emplace_back(cid, enabled);

        if (!enabled)
        {
            enabled_cids_.erase(cid);
            refreshable_cids_.erase(cid);
            countdowns_to_refresh_.erase(cid);

            if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
            {
                auto checkpoint = checkpointer->createDisabledCheckpoint();
                latest_checkpoints_[cid] = checkpoint;
                waiting_queue_.back().checkpoints[cid] = checkpoint;
            }
        } else
        {
            enabled_cids_.insert(cid);
            refreshable_cids_.insert(cid);
            countdowns_to_refresh_[cid] = heartbeat_;

            if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
            {
                auto checkpoint = checkpointer->createReenabledCheckpoint();
                latest_checkpoints_[cid] = checkpoint;
                waiting_queue_.back().checkpoints[cid] = checkpoint;
            }
        }
    }

    void onQuietChanged(uint16_t cid, bool quiet)
    {
        advanceSimTimeSlot();
        waiting_queue_.back().quiet_changes.emplace_back(cid, quiet);

        if (quiet)
        {
            refreshable_cids_.erase(cid);
            countdowns_to_refresh_.erase(cid);

            if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
            {
                auto checkpoint = checkpointer->createQuietedCheckpoint();
                latest_checkpoints_[cid] = checkpoint;
                waiting_queue_.back().checkpoints[cid] = checkpoint;
            }
        } else if (enabled_cids_.count(cid) > 0)
        {
            refreshable_cids_.insert(cid);
            countdowns_to_refresh_[cid] = heartbeat_;

            if (auto* checkpointer = findScalarCheckpointer_(cid); checkpointer && checkpointer->tip())
            {
                auto checkpoint = checkpointer->createReenabledCheckpoint();
                latest_checkpoints_[cid] = checkpoint;
                waiting_queue_.back().checkpoints[cid] = checkpoint;
            }
        }
    }

private:
    static constexpr auto kCidBytes = sizeof(uint16_t);
    static constexpr auto kActionBytes = sizeof(uint8_t);

    static bool isLifecycleAction_(const CollectedData& data)
    {
        const auto& bytes = data.getData();
        if (bytes.size() < kCidBytes + kActionBytes)
        {
            return false;
        }

        uint8_t raw_action = 0;
        memcpy(&raw_action, bytes.data() + kCidBytes, kActionBytes);
        return raw_action < static_cast<uint8_t>(Action::FULL);
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

    bool appendIfChanged_(QueueCollectionData& to_send, uint16_t cid, const std::shared_ptr<const Checkpoint>& checkpoint)
    {
        auto data = checkpoint->getMinifiedData();
        const auto& bytes = data->getData();
        if (bytes.size() >= kCidBytes + kActionBytes)
        {
            const uint8_t action = static_cast<uint8_t>(bytes[kCidBytes]);
            if (action == static_cast<uint8_t>(Action::CARRY))
            {
                if (auto it = last_sent_bytes_.find(cid); it != last_sent_bytes_.end() && it->second == bytes)
                {
                    return false;
                }
            }
        }

        to_send.collection_data.emplace_back(std::move(data));
        return true;
    }

    static bool resetsRefreshCountdown_(const CollectedData& data)
    {
        const auto& bytes = data.getData();
        if (bytes.size() < kCidBytes + kActionBytes)
        {
            return false;
        }

        uint8_t action = 0;
        memcpy(&action, bytes.data() + kCidBytes, kActionBytes);
        return action == static_cast<uint8_t>(Action::FULL);
    }

    void recordSentData_(uint16_t cid, const CollectedData& data)
    {
        last_sent_bytes_[cid] = data.getData();
        if (resetsRefreshCountdown_(data))
        {
            countdowns_to_refresh_[cid] = heartbeat_;
        }
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

    void sendToPipeline_(QueueCollectionData& collection_at_time)
    {
        QueueCollectionData to_send;
        to_send.sim_time = collection_at_time.sim_time;

        for (const auto& [cid, checkpoint] : collection_at_time.checkpoints)
        {
            if (appendIfChanged_(to_send, cid, checkpoint))
            {
                latest_checkpoints_[cid] = checkpoint;
            }
        }

        auto missing_cids = refreshable_cids_;
        for (const auto& [cid, _] : collection_at_time.checkpoints)
        {
            missing_cids.erase(cid);
        }

        for (auto& data : to_send.collection_data)
        {
            if (!data || isLifecycleAction_(*data))
            {
                continue;
            }

            const auto cid = data->getCID();
            missing_cids.erase(cid);
            recordSentData_(cid, *data);
        }

        for (auto cid : missing_cids)
        {
            if (std::any_of(
                    to_send.collection_data.begin(), to_send.collection_data.end(),
                    [cid](const std::unique_ptr<CollectedData>& data) { return data && data->getCID() == cid; }))
            {
                continue;
            }

            auto countdown_it = countdowns_to_refresh_.find(cid);
            if (countdown_it == countdowns_to_refresh_.end())
            {
                continue;
            }

            assert(countdown_it->second > 0);
            if (--countdown_it->second == 0)
            {
                auto latest_it = latest_checkpoints_.find(cid);
                assert(latest_it != latest_checkpoints_.end());
                auto injected = latest_it->second->getFullData();
                recordSentData_(cid, *injected);
                countdowns_to_refresh_[cid] = heartbeat_;
                to_send.collection_data.emplace_back(std::move(injected));
            }
        }

        if (!to_send.collection_data.empty())
        {
            pipeline_head_->emplace(std::move(to_send));
        }
    }

    const size_t heartbeat_;
    Timestamp* const timestamp_;
    ConcurrentQueue<QueueCollectionData>* const pipeline_head_;
    ConcurrentQueue<Notification>* const notif_head_;
    ConcurrentQueue<DynamicFieldChanges>* const dyn_field_head_;

    std::queue<QueueCollectionData> waiting_queue_;
    ValidValue<uint64_t> last_stage_time_;
    bool auto_send_when_time_advances_ = true;

    std::unordered_set<uint16_t> scalar_cids_;
    std::unordered_map<uint16_t, std::unique_ptr<ScalarCheckpointer>> scalar_checkpointers_;
    std::unordered_map<uint16_t, std::shared_ptr<const Checkpoint>> latest_checkpoints_;

    std::unordered_set<uint16_t> enabled_cids_;
    std::unordered_set<uint16_t> refreshable_cids_;
    std::unordered_map<uint16_t, size_t> countdowns_to_refresh_;
    std::unordered_map<uint16_t, std::vector<char>> last_sent_bytes_;
};

} // namespace simdb::argos
