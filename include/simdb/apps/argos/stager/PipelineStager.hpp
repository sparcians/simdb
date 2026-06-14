// <PipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/stager/Checkpointer.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace simdb::argos {

//! \class PipelineStager
//! \brief Checkpoint-based collection stager.
class PipelineStager
{
public:
    PipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* pipeline_head,
                   ConcurrentQueue<Notification>* notif_head = nullptr,
                   ConcurrentQueue<DynamicFieldChanges>* dyn_field_head = nullptr) :
        heartbeat_(heartbeat),
        timestamp_(timestamp),
        pipeline_head_(pipeline_head),
        notif_head_(notif_head),
        dyn_field_head_(dyn_field_head)
    {
        assert(heartbeat_ > 0);
        assert(timestamp_ != nullptr);
    }

    size_t getHeartbeat() const { return heartbeat_; }

    void setScalarType(uint16_t cid)
    {
        checkpointers_[cid] = std::make_unique<ScalarCheckpointer>(heartbeat_);
    }

    void setContainerType(uint16_t cid, bool sparse, size_t capacity)
    {
        if (sparse)
        {
            checkpointers_[cid] = std::make_unique<SparseContainerCheckpointer>(heartbeat_, capacity);
        } else
        {
            checkpointers_[cid] = std::make_unique<ContigContainerCheckpointer>(heartbeat_, capacity);
        }
    }

    void stage(uint16_t cid, const std::vector<char>& scalar_bytes)
    {
        getCurrentTime_();
        checkpointers_.at(cid)->createCheckpoint(current_window_id_, scalar_bytes);
    }

    void stage(uint16_t cid, const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        getCurrentTime_();
        checkpointers_.at(cid)->createCheckpoint(current_window_id_, contig_bin_bytes);
    }

    void stage(uint16_t cid, const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        getCurrentTime_();
        checkpointers_.at(cid)->createCheckpoint(current_window_id_, sparse_bin_bytes);
    }

    void onEnabledChanged(uint16_t cid, bool enabled)
    {
        getCurrentTime_();
        checkpointers_.at(cid)->onEnabledChanged(current_window_id_, enabled);
    }

    void onQuietChanged(uint16_t cid, bool quiet)
    {
        getCurrentTime_();
        checkpointers_.at(cid)->onQuietChanged(current_window_id_, quiet);
    }

    void sendCollectedDataToPipeline()
    {
        while (!ready_queue_.empty())
        {
            const auto& ready = ready_queue_.front();
            sendToPipeline_(ready.sim_time, ready.window_id);
            ready_queue_.pop();
        }
    }

    void disableAutoSendMode(bool disable = true) { auto_send_ = !disable; }

    void postNotif(uint16_t cid, const std::string& notif, NotifType type)
    {
        if (notif_head_ == nullptr)
        {
            return;
        }

        if (timestamp_ != nullptr)
        {
            Notification notification(cid, notif, type, timestamp_->getTime());
            notif_head_->emplace(std::move(notification));
        } else
        {
            Notification notification(cid, notif, type);
            notif_head_->emplace(std::move(notification));
        }
    }

    void postDynamicFieldChanges(uint16_t cid, const std::vector<std::string>& field_names,
                                 const std::vector<MinimalType>& field_types)
    {
        assert(dyn_field_head_ != nullptr);
        DynamicFieldChanges changes(cid, field_names, field_types, timestamp_->getTime());
        dyn_field_head_->emplace(std::move(changes));
    }

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr)
    {
        for (const auto& [cid, checkpointer] : checkpointers_)
        {
            checkpointer->writeMetaOnPostTeardown(cid, db_mgr);
        }
    }

private:
    uint64_t getCurrentTime_()
    {
        auto current_time = timestamp_->getTime();
        if (!current_stage_time_.isValid())
        {
            current_stage_time_ = current_time;
        } else if (current_time < current_stage_time_.getValue())
        {
            throw DBException("Time must be monotonically increasing");
        } else if (current_time > current_stage_time_.getValue())
        {
            Ready ready(current_stage_time_, current_window_id_++);
            ready_queue_.emplace(std::move(ready));

            if (auto_send_)
            {
                sendCollectedDataToPipeline();
            }
        }

        current_stage_time_ = current_time;
        return current_time;
    }

    void sendToPipeline_(uint64_t sim_time, uint64_t window_id)
    {
        QueueCollectionData to_send;
        to_send.sim_time = sim_time;

        for (auto& [cid, checkpointer] : checkpointers_)
        {
            if (auto entry = checkpointer->encodeForPipeline(window_id, cid))
            {
                to_send.entries.emplace_back(std::move(entry));
            }
        }

        if (!to_send.entries.empty())
        {
            pipeline_head_->emplace(std::move(to_send));
        }
    }

    const size_t heartbeat_;
    Timestamp* const timestamp_;
    ValidValue<uint64_t> current_stage_time_;
    uint64_t current_window_id_ = 1;
    bool auto_send_ = true;

    ConcurrentQueue<QueueCollectionData>* const pipeline_head_;
    ConcurrentQueue<Notification>* const notif_head_;
    ConcurrentQueue<DynamicFieldChanges>* const dyn_field_head_;

    std::unordered_map<uint16_t, std::unique_ptr<CollectableCheckpointer>> checkpointers_;

    struct Ready {
        uint64_t sim_time = 0;
        uint64_t window_id = 0;

        Ready(uint64_t sim_time, uint64_t window_id)
            : sim_time(sim_time)
            , window_id(window_id)
        {}
    };

    std::queue<Ready> ready_queue_;
};

} // namespace simdb::argos
