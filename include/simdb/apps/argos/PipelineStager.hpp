// <PipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/apps/argos/LifecycleAction.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"
#include "simdb/utils/Demangle.hpp"

#include <map>
#include <queue>

namespace simdb::argos {

class PipelineStager
{
public:
    PipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* pipeline_head,
                   ConcurrentQueue<Notification>* notif_head, ConcurrentQueue<DynamicFieldChanges>* dyn_field_head) :
        heartbeat_(heartbeat),
        timestamp_(timestamp),
        pipeline_head_(pipeline_head),
        notif_head_(notif_head),
        dyn_field_head_(dyn_field_head)
    {
    }

    void throwOnAnyActivity(uint16_t cid) { throw_on_any_activity_.insert(cid); }

    void stage(CollectedData&& data)
    {
        auto cid = data.getCID();
        assert(cid != 0);
        throwIfBadID_(cid);
        enabled_cids_.insert(cid);
        refreshable_cids_.insert(cid);

        if (advanceStageTime_())
        {
            QueueCollectionData entry;
            entry.sim_time = last_stage_time_.getValue();
            entry.collection_data.emplace_back(std::make_unique<CollectedData>(std::move(data)));
            waiting_queue_.emplace(std::move(entry));
        } else
        {
            CollectionDataAtTimePoint& collection = waiting_queue_.back().collection_data;
            collection.emplace_back(std::make_unique<CollectedData>(std::move(data)));
        }
    }

    void sendCollectedDataToPipeline()
    {
        while (!waiting_queue_.empty())
        {
            sendToPipeline_(waiting_queue_.front());
            waiting_queue_.pop();
        }
    }

    void onEnabledChanged(uint16_t cid, bool enabled)
    {
        throwIfBadID_(cid);
        if (advanceStageTime_())
        {
            QueueCollectionData entry;
            entry.sim_time = last_stage_time_.getValue();
            entry.enabled_changes.emplace_back(std::make_pair(cid, enabled));
            waiting_queue_.emplace(std::move(entry));
        } else
        {
            EnabledChangedAtTimePoint& changes = waiting_queue_.back().enabled_changes;
            changes.emplace_back(std::make_pair(cid, enabled));
        }
    }

    void onQuietChanged(uint16_t cid, bool quiet)
    {
        throwIfBadID_(cid);
        if (advanceStageTime_())
        {
            QueueCollectionData entry;
            entry.sim_time = last_stage_time_.getValue();
            entry.quiet_changes.emplace_back(std::make_pair(cid, quiet));
            waiting_queue_.emplace(std::move(entry));
        } else
        {
            QuietChangedAtTimePoint& changes = waiting_queue_.back().quiet_changes;
            changes.emplace_back(std::make_pair(cid, quiet));
        }
    }

    void postNotif(uint16_t cid, const std::string& notif, NotifType type)
    {
        throwIfBadID_(cid);
        ValidValue<uint64_t> sim_time;
        if (timestamp_)
        {
            sim_time = timestamp_->getTime();
        }
        Notification notification(cid, notif, type, sim_time);
        notif_head_->emplace(std::move(notification));
    }

    void postDynamicFieldChanges(uint16_t cid, const std::vector<std::string>& field_names,
                                 const std::vector<MinimalType>& field_types)
    {
        throwIfBadID_(cid);
        assert(timestamp_ != nullptr);
        DynamicFieldChanges changes(cid, field_names, field_types, timestamp_->getTime());
        dyn_field_head_->emplace(std::move(changes));
    }

private:
    static constexpr auto kCidBytes = sizeof(uint16_t);
    static constexpr auto kActionBytes = sizeof(uint8_t);
    static constexpr auto kFirstMinifierAction = FULL_ACTION_FLAG;

    static bool isLifecycleAction_(const std::vector<char>& data)
    {
        if (data.size() < kCidBytes + kActionBytes)
        {
            return false;
        }

        uint8_t raw_action = 0;
        memcpy(&raw_action, data.data() + kCidBytes, kActionBytes);
        return raw_action < FULL_ACTION_FLAG;
    }

    void queueLifecycleAction_(QueueCollectionData& collection_data, uint16_t cid, LifecycleAction action,
                               bool append_last_payload)
    {
        const std::vector<char>* full_payload = nullptr;
        if (append_last_payload)
        {
            auto it = last_full_payload_bytes_.find(cid);
            if (it == last_full_payload_bytes_.end())
            {
                // We cannot emit ENABLED/AWAKENED without an attached
                // payload tail, otherwise downstream replay will desync.
                return;
            }
            full_payload = &it->second;
        }

        auto lifecycle = std::make_unique<CollectedData>(cid);
        auto& buf = lifecycle->getBuffer();
        const auto raw_action = static_cast<uint8_t>(action);
        buf.append(raw_action);

        if (full_payload && !full_payload->empty())
        {
            // ENABLED/AWAKENED append only the full payload bytes. The lifecycle
            // action itself is already encoded in this record's action byte.
            buf.append(*full_payload);
        }

        collection_data.collection_data.emplace_back(std::move(lifecycle));
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

        // Tell the call site whether we should start a brand new "slot" in the waiting queue.
        if (waiting_queue_.empty())
        {
            return true;
        }

        auto prev_slot_time = waiting_queue_.back().sim_time;
        if (current_time == prev_slot_time)
        {
            return false;
        } else if (current_time < prev_slot_time)
        {
            throw DBException("Time must be monotonically increasing");
        }

        return true;
    }

    void sendToPipeline_(QueueCollectionData& collection_at_time)
    {
        // To account for the use case where the same collectable is collected
        // multiple times at the same time point, only take the last collected
        // value.
        std::map<uint16_t, std::unique_ptr<CollectedData>> collected_data_by_cid;
        for (auto rit = collection_at_time.collection_data.rbegin(); rit != collection_at_time.collection_data.rend();
             ++rit)
        {
            auto cid = (*rit)->getCID();
            auto& collected_data = collected_data_by_cid[cid];
            if (!collected_data)
            {
                collected_data = std::move(*rit);
            }
        }

        collection_at_time.collection_data.clear();
        for (auto& [cid, collected_data] : collected_data_by_cid)
        {
            collection_at_time.collection_data.emplace_back(std::move(collected_data));
        }

        QueueCollectionData to_send;
        to_send.sim_time = collection_at_time.sim_time;

        // Take into account whether the collected data has changed
        for (auto& data : collection_at_time.collection_data)
        {
            auto cid = data->getCID();
            if (auto it = last_sent_bytes_.find(cid); it != last_sent_bytes_.end())
            {
                if (it->second == data->getData())
                {
                    continue;
                }
            }
            to_send.collection_data.emplace_back(std::move(data));
        }

        // Update our data structures to account for enabled/disabled
        // changes at this time point.
        for (const auto& [cid, enabled] : collection_at_time.enabled_changes)
        {
            if (!enabled)
            {
                // Disabled CIDs cannot be collected or heartbeat-refreshed.
                enabled_cids_.erase(cid);
                refreshable_cids_.erase(cid);
                countdowns_to_refresh_.erase(cid);
                queueLifecycleAction_(to_send, cid, LifecycleAction::DISABLED, false);
            } else
            {
                // Re-enabled CIDs are again eligible to refresh.
                enabled_cids_.insert(cid);
                refreshable_cids_.insert(cid);
                countdowns_to_refresh_[cid] = heartbeat_;
                queueLifecycleAction_(to_send, cid, LifecycleAction::ENABLED, true);
            }
        }

        // Apply quiet/awaken transitions after enabled changes so the
        // final state at this time point is unambiguous.
        for (const auto& [cid, quiet] : collection_at_time.quiet_changes)
        {
            if (quiet)
            {
                refreshable_cids_.erase(cid);
                countdowns_to_refresh_.erase(cid);
                queueLifecycleAction_(to_send, cid, LifecycleAction::QUIETED, false);
            } else if (enabled_cids_.find(cid) != enabled_cids_.end())
            {
                refreshable_cids_.insert(cid);
                countdowns_to_refresh_[cid] = heartbeat_;
                queueLifecycleAction_(to_send, cid, LifecycleAction::AWAKENED, true);
            }
        }

        // Periodically dump "last seen bytes" for any CIDs not encountered
        // at this time point (enabled+awake but not collected)
        auto missing_cids = refreshable_cids_;
        for (const auto& data : collection_at_time.collection_data)
        {
            if (!data)
            {
                continue;
            }
            if (isLifecycleAction_(data->getData()))
            {
                continue;
            }
            missing_cids.erase(data->getCID());
        }

        for (auto& data : to_send.collection_data)
        {
            if (isLifecycleAction_(data->getData()))
            {
                continue;
            }
            auto cid = data->getCID();
            countdowns_to_refresh_[cid] = heartbeat_;
            const auto& data_bytes = data->getData();
            last_sent_bytes_[cid] = data_bytes;

            if (data_bytes.size() >= kCidBytes + kActionBytes)
            {
                uint8_t action = 0;
                memcpy(&action, data_bytes.data() + kCidBytes, kActionBytes);
                if (action == kFirstMinifierAction)
                {
                    // Persist only FULL payload bytes for lifecycle re-entry.
                    const auto payload_begin =
                        data_bytes.begin() + static_cast<std::ptrdiff_t>(kCidBytes + kActionBytes);
                    last_full_payload_bytes_[cid] = std::vector<char>(payload_begin, data_bytes.end());
                }
            }
        }

        for (auto cid : missing_cids)
        {
            if (std::any_of(
                    to_send.collection_data.begin(), to_send.collection_data.end(),
                    [cid](const std::unique_ptr<CollectedData>& data) { return data && data->getCID() == cid; }))
            {
                // This CID already emitted a record at this time point (e.g.
                // fresh FULL data or lifecycle replay payload). Avoid emitting
                // a duplicate heartbeat replay record for the same tick.
                continue;
            }

            auto it = countdowns_to_refresh_.find(cid);
            if (it == countdowns_to_refresh_.end())
            {
                continue;
            }

            assert(it->second > 0);
            if (--it->second == 0)
            {
                // The CollectedData object will immediately add the uint16_t cid
                // to the underlying buffer. Our last_sent_bytes_ also has the
                // cid at the head of the bytes. That's why we are using the
                // StreamBuffer::append() api below with a uint16_t offset.
                auto injected_data = std::make_unique<CollectedData>(cid);
                const auto& last_sent_bytes = last_sent_bytes_.at(cid);
                const auto src = last_sent_bytes.data() + sizeof(uint16_t);
                const auto src_bytes = last_sent_bytes.size() - sizeof(uint16_t);
                auto& buffer = injected_data->getBuffer();
                buffer.append(src, src_bytes);
                to_send.collection_data.emplace_back(std::move(injected_data));
                countdowns_to_refresh_[cid] = heartbeat_;
            }
        }

        // Send everything to the pipeline
        if (!to_send.collection_data.empty())
        {
            pipeline_head_->emplace(std::move(to_send));
        }
    }

    void throwIfBadID_(uint16_t cid) const
    {
        if (throw_on_any_activity_.count(cid) > 0)
        {
            throw DBException("Collectable with ID ") << cid << " is not allowed to perform any collection activity.";
        }
    }

    const size_t heartbeat_;
    Timestamp* const timestamp_;
    ConcurrentQueue<QueueCollectionData>* const pipeline_head_;
    ConcurrentQueue<Notification>* const notif_head_;
    ConcurrentQueue<DynamicFieldChanges>* const dyn_field_head_;
    std::queue<QueueCollectionData> waiting_queue_;
    ValidValue<uint64_t> last_stage_time_;
    std::unordered_set<uint16_t> enabled_cids_;
    std::unordered_set<uint16_t> refreshable_cids_;
    std::unordered_map<uint16_t, size_t> countdowns_to_refresh_;
    std::unordered_map<uint16_t, std::vector<char>> last_sent_bytes_;
    std::unordered_map<uint16_t, std::vector<char>> last_full_payload_bytes_;
    std::unordered_set<uint16_t> throw_on_any_activity_;
};

} // namespace simdb::argos
