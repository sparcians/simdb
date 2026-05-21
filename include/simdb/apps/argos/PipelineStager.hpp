// <PipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/apps/argos/LifecycleAction.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/ConcurrentQueue.hpp"

#include <map>
#include <queue>

namespace simdb::argos {

using CollectionDataAtTimePoint = std::vector<std::unique_ptr<CollectedData>>;
using EnabledChangedAtTimePoint = std::vector<std::pair<uint16_t, bool>>;
using QuietChangedAtTimePoint = std::vector<std::pair<uint16_t, bool>>;
using CollectionTime = std::shared_ptr<TimePointBase>;

struct QueueCollectionData
{
    CollectionTime time_point;
    CollectionDataAtTimePoint collection_data;
    EnabledChangedAtTimePoint enabled_changes;
    QuietChangedAtTimePoint quiet_changes;
};

enum class NotifType { WARNING, ERROR, MESSAGE, __INVALID__ };

struct Notification
{
    uint16_t cid = 0;
    std::string notif;
    NotifType type = NotifType::__INVALID__;
    std::shared_ptr<TimePointBase> time_point;

    Notification(uint16_t cid,
                 const std::string& notif,
                 const NotifType type,
                 std::shared_ptr<TimePointBase> && time_point)
        : cid(cid)
        , notif(notif)
        , type(type)
        , time_point(time_point)
    {}

    // Default ctor needed for ConcurrentQueue::try_pop
    Notification() = default;
};

class PipelineStagerBase
{
public:
    virtual ~PipelineStagerBase() = default;
    virtual void stage(CollectedData&& data) = 0;
    virtual void sendCollectedDataToPipeline() = 0;
    virtual void onEnabledChanged(uint16_t cid, bool enabled) = 0;
    virtual void onQuietChanged(uint16_t cid, bool quiet) = 0;
    virtual std::string getTimeAsString() const = 0;
    virtual void postNotif(uint16_t cid, const std::string& notif, NotifType type, bool timestamp = true) = 0;
};

template <typename TimeT> class PipelineStager final : public PipelineStagerBase
{
public:
    PipelineStager(size_t heartbeat,
                   Timestamp<TimeT>* timestamp,
                   ConcurrentQueue<QueueCollectionData>* pipeline_head,
                   ConcurrentQueue<Notification>* notif_head)
        : heartbeat_(heartbeat)
        , timestamp_(timestamp)
        , pipeline_head_(pipeline_head)
        , notif_head_(notif_head)
    {
    }

    void stage(CollectedData&& data) override
    {
        auto cid = data.getCID();
        assert(cid != 0);
        enabled_cids_.insert(cid);
        refreshable_cids_.insert(cid);

        auto current_time = timestamp_->snapshot();
        if (!last_stage_time_)
        {
            last_stage_time_ = current_time;
        } else if (!current_time->lessThan(last_stage_time_.get()))
        {
            last_stage_time_ = current_time;
        } else
        {
            throw DBException("Time must be monotonically increasing");
        }

        if (!waiting_queue_.empty() && waiting_queue_.back().time_point->equals(current_time.get(), true))
        {
            CollectionDataAtTimePoint& collection = waiting_queue_.back().collection_data;
            collection.emplace_back(std::make_unique<CollectedData>(std::move(data)));
        } else
        {
            QueueCollectionData entry;
            entry.time_point = current_time;
            entry.collection_data.emplace_back(std::make_unique<CollectedData>(std::move(data)));
            waiting_queue_.emplace(std::move(entry));
        }
    }

    void sendCollectedDataToPipeline() override
    {
        while (!waiting_queue_.empty())
        {
            sendToPipeline_(waiting_queue_.front());
            waiting_queue_.pop();
        }
    }

    void onEnabledChanged(uint16_t cid, bool enabled) override
    {
        auto current_time = timestamp_->snapshot();
        if (!last_stage_time_)
        {
            last_stage_time_ = current_time;
        } else if (!current_time->lessThan(last_stage_time_.get()))
        {
            last_stage_time_ = current_time;
        } else
        {
            throw DBException("Time must be monotonically increasing");
        }

        if (!waiting_queue_.empty() && waiting_queue_.back().time_point->equals(current_time.get(), true))
        {
            EnabledChangedAtTimePoint& changes = waiting_queue_.back().enabled_changes;
            changes.emplace_back(std::make_pair(cid, enabled));
        } else
        {
            QueueCollectionData entry;
            entry.time_point = current_time;
            entry.enabled_changes.emplace_back(std::make_pair(cid, enabled));
            waiting_queue_.emplace(std::move(entry));
        }
    }

    void onQuietChanged(uint16_t cid, bool quiet) override
    {
        auto current_time = timestamp_->snapshot();
        if (!last_stage_time_)
        {
            last_stage_time_ = current_time;
        } else if (!current_time->lessThan(last_stage_time_.get()))
        {
            last_stage_time_ = current_time;
        } else
        {
            throw DBException("Time must be monotonically increasing");
        }

        if (!waiting_queue_.empty() && waiting_queue_.back().time_point->equals(current_time.get(), true))
        {
            QuietChangedAtTimePoint& changes = waiting_queue_.back().quiet_changes;
            changes.emplace_back(std::make_pair(cid, quiet));
        } else
        {
            QueueCollectionData entry;
            entry.time_point = current_time;
            entry.quiet_changes.emplace_back(std::make_pair(cid, quiet));
            waiting_queue_.emplace(std::move(entry));
        }
    }

    std::string getTimeAsString() const override { return timestamp_->getTimeAsString(); }

    void postNotif(uint16_t cid, const std::string& notif, NotifType type, bool timestamp = true) override
    {
        Notification notification(cid, notif, type, timestamp ? timestamp_->snapshot() : nullptr);
        notif_head_->emplace(std::move(notification));
    }

private:
    static constexpr auto kCidBytes = sizeof(uint16_t);
    static constexpr auto kActionBytes = sizeof(uint8_t);
    static constexpr auto kFirstMinifierAction = static_cast<uint8_t>(LifecycleAction::__FIRST_MINIFIER_ACTION);

    static bool isLifecycleAction_(const std::vector<char>& data)
    {
        if (data.size() < kCidBytes + kActionBytes)
        {
            return false;
        }

        uint8_t raw_action = 0;
        std::memcpy(&raw_action, data.data() + kCidBytes, kActionBytes);
        return raw_action < static_cast<uint8_t>(LifecycleAction::__FIRST_MINIFIER_ACTION);
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
        to_send.time_point = collection_at_time.time_point;

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

        // Periodically dump "last seen bytes" for any CIDs not
        // encountered at this time point (enabled+awake but not collected)
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
                std::memcpy(&action, data_bytes.data() + kCidBytes, kActionBytes);
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

    const size_t heartbeat_;
    Timestamp<TimeT>* const timestamp_;
    ConcurrentQueue<QueueCollectionData>* const pipeline_head_;
    ConcurrentQueue<Notification>* const notif_head_;
    std::queue<QueueCollectionData> waiting_queue_;
    CollectionTime last_stage_time_;
    std::unordered_set<uint16_t> enabled_cids_;
    std::unordered_set<uint16_t> refreshable_cids_;
    std::unordered_map<uint16_t, size_t> countdowns_to_refresh_;
    std::unordered_map<uint16_t, std::vector<char>> last_sent_bytes_;
    std::unordered_map<uint16_t, std::vector<char>> last_full_payload_bytes_;
};

} // namespace simdb::argos
