// <PipelineStager.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/Checkpointer.hpp"
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
    PipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* sync_pipeline_head,
                   ConcurrentQueue<Notification>* notif_head = nullptr,
                   ConcurrentQueue<DynamicFieldChanges>* dyn_field_head = nullptr) :
        PipelineStager(heartbeat, timestamp, sync_pipeline_head, nullptr, false, nullptr, notif_head, dyn_field_head)
    {
    }

    PipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* sync_pipeline_head,
                   ConcurrentQueue<DeltaEncodingBatch>* async_pipeline_head, bool async_encoding,
                   ConcurrentQueue<Notification>* notif_head = nullptr,
                   ConcurrentQueue<DynamicFieldChanges>* dyn_field_head = nullptr) :
        PipelineStager(heartbeat, timestamp, sync_pipeline_head, async_pipeline_head, async_encoding, nullptr,
                       notif_head, dyn_field_head)
    {
    }

    PipelineStager(size_t heartbeat, Timestamp* timestamp, ConcurrentQueue<QueueCollectionData>* sync_pipeline_head,
                   ConcurrentQueue<DeltaEncodingBatch>* async_pipeline_head, bool async_encoding,
                   ConcurrentQueue<AsyncEncodeCompletion>* async_encode_completion_queue,
                   ConcurrentQueue<Notification>* notif_head = nullptr,
                   ConcurrentQueue<DynamicFieldChanges>* dyn_field_head = nullptr) :
        heartbeat_(heartbeat),
        timestamp_(timestamp),
        sync_pipeline_head_(sync_pipeline_head),
        async_pipeline_head_(async_pipeline_head),
        async_encoding_(async_encoding),
        async_encode_completion_queue_(async_encode_completion_queue),
        notif_head_(notif_head),
        dyn_field_head_(dyn_field_head)
    {
        assert(heartbeat_ > 0);
        assert(timestamp_ != nullptr);
        if (async_encoding_)
        {
            assert(async_pipeline_head_ != nullptr);
        } else
        {
            assert(sync_pipeline_head_ != nullptr);
        }
    }

    size_t getHeartbeat() const { return heartbeat_; }

    void setCollectableClock(uint16_t cid, uint32_t clock_id)
    {
        collectable_clock_ids_[cid] = clock_id;
        clock_ids_.insert(clock_id);
    }

    void setScalarType(uint16_t cid) { checkpointers_[cid] = std::make_unique<ScalarCheckpointer>(heartbeat_); }

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
        drainAsyncEncodeCompletions();
        while (!ready_queue_.empty())
        {
            const auto& ready = ready_queue_.front();
            sendToPipeline_(ready.sim_time, ready.window_id);
            ready_queue_.pop();
        }
        drainAsyncEncodeCompletions();
    }

    void drainAsyncEncodeCompletions()
    {
        if (async_encode_completion_queue_ == nullptr)
        {
            return;
        }

        AsyncEncodeCompletion completion;
        while (async_encode_completion_queue_->try_pop(completion))
        {
            checkpointers_.at(completion.cid)
                ->commitAsyncEncode(completion.tail, completion.release_through_full_anchor);
        }
    }

    void disableAutoSendMode(bool disable = true) { auto_send_ = !disable; }

    void advanceSimTimeSlot() { getCurrentTime_(); }

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
        writeMetaForSentCids(db_mgr, wire_sent_cids_);
        writeCheckpointerMetaOnPostTeardown(db_mgr);
    }

    void writeCheckpointerMetaOnPostTeardown(DatabaseManager* db_mgr) const
    {
        for (const auto& [cid, checkpointer] : checkpointers_)
        {
            checkpointer->writeMetaOnPostTeardown(cid, db_mgr);
        }
    }

    static void writeMetaForSentCids(DatabaseManager* db_mgr, const std::unordered_set<uint16_t>& wire_sent_cids)
    {
        std::vector<int> valid_cids;
        valid_cids.reserve(wire_sent_cids.size());
        for (const auto cid : wire_sent_cids)
        {
            valid_cids.push_back(cid);
        }

        if (!valid_cids.empty())
        {
            std::ostringstream oss;
            oss << "UPDATE CollectableTreeNodes SET ShowInUI=1 WHERE SerializationCID IN (";

            bool comma = false;
            for (const auto cid : valid_cids)
            {
                if (comma)
                {
                    oss << ",";
                }
                oss << cid;
                comma = true;
            }
            oss << ")";
            db_mgr->EXECUTE(oss.str());
        }

        auto query = db_mgr->createQuery("CollectableTreeNodes");
        query->addConstraintForInt("SerializationCID", SetConstraints::NOT_IN_SET, valid_cids);

        struct CID_Info
        {
            std::string path;
            std::string type;

            CID_Info(const std::string& path, const std::string& type) :
                path(path),
                type(type)
            {
            }
        };

        std::string path;
        query->select("FullPath", path);

        std::string type;
        query->select("TypeName", type);

        std::vector<CID_Info> cid_infos;
        auto results = query->getResultSet();
        while (results.getNextRecord())
        {
            cid_infos.emplace_back(path, type);
        }

        if (!cid_infos.empty())
        {
            std::ostringstream oss;
            oss << "No data was ever collected for the following collectables, and will not be shown in Argos:\n";
            size_t leftcol_w = 0;
            for (const auto& info : cid_infos)
            {
                leftcol_w = std::max(leftcol_w, info.path.size());
            }

            leftcol_w += 12;
            for (const auto& info : cid_infos)
            {
                oss << std::left << std::setw(leftcol_w) << info.path;
                if (auto idx = info.type.find("_sparse"); idx != std::string::npos)
                {
                    auto base_type = info.type.substr(0, idx);
                    oss << "(Sparse container of '" << base_type << "')";
                } else if (auto idx = info.type.find("_contig"); idx != std::string::npos)
                {
                    auto base_type = info.type.substr(0, idx);
                    oss << "(Contig container of '" << base_type << "')";
                } else
                {
                    oss << "(" << info.type << ")";
                }
                oss << "\n";
            }

            auto warning = oss.str();
            warning.pop_back();

            constexpr auto no_cid = 0;
            db_mgr->INSERT(SQL_TABLE("Notifications"), SQL_COLUMNS("SerializationCID", "NotifStr", "NotifType"),
                           SQL_VALUES(no_cid, warning, (int)NotifType::WARNING));
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

    bool hasMultipleCollectableClocks_() const { return clock_ids_.size() > 1; }

    void sendToPipeline_(uint64_t sim_time, uint64_t window_id)
    {
        if (async_encoding_)
        {
            sendToPipelineAsync_(sim_time, window_id);
        } else
        {
            sendToPipelineSync_(sim_time, window_id);
        }
    }

    void sendToPipelineSync_(uint64_t sim_time, uint64_t window_id)
    {
        QueueCollectionData to_send;
        to_send.sim_time = sim_time;

        const bool multi_clock = hasMultipleCollectableClocks_();

        for (auto& [cid, checkpointer] : checkpointers_)
        {
            if (multi_clock && !checkpointer->participatedInWindow(window_id))
            {
                continue;
            }

            auto wires = checkpointer->encodeForPipeline(window_id, sim_time, cid);
            for (auto& entry : wires)
            {
                wire_sent_cids_.insert(cid);
                to_send.entries.emplace_back(std::move(entry));
                if (const auto clk_it = collectable_clock_ids_.find(cid); clk_it != collectable_clock_ids_.end())
                {
                    to_send.clock_ids.insert(clk_it->second);
                }
            }
        }

        sync_pipeline_head_->emplace(std::move(to_send));
    }

    void sendToPipelineAsync_(uint64_t sim_time, uint64_t window_id)
    {
        drainAsyncEncodeCompletions();

        DeltaEncodingBatch batch;
        batch.sim_time = sim_time;
        batch.window_id = window_id;
        batch.cid_to_clock = collectable_clock_ids_;

        const bool multi_clock = hasMultipleCollectableClocks_();

        for (auto& [cid, checkpointer] : checkpointers_)
        {
            if (multi_clock && !checkpointer->participatedInWindow(window_id))
            {
                continue;
            }

            if (auto work = checkpointer->buildEncodeWork(window_id, cid))
            {
                batch.work.emplace_back(std::move(*work));
            }
        }

        async_pipeline_head_->emplace(std::move(batch));
    }

    const size_t heartbeat_;
    Timestamp* const timestamp_;
    ValidValue<uint64_t> current_stage_time_;
    uint64_t current_window_id_ = 1;
    bool auto_send_ = true;

    ConcurrentQueue<QueueCollectionData>* const sync_pipeline_head_;
    ConcurrentQueue<DeltaEncodingBatch>* const async_pipeline_head_;
    const bool async_encoding_;
    ConcurrentQueue<AsyncEncodeCompletion>* const async_encode_completion_queue_;
    ConcurrentQueue<Notification>* const notif_head_;
    ConcurrentQueue<DynamicFieldChanges>* const dyn_field_head_;

    std::map<uint16_t, std::unique_ptr<CollectableCheckpointer>> checkpointers_;
    std::unordered_map<uint16_t, uint32_t> collectable_clock_ids_;
    std::unordered_set<uint16_t> wire_sent_cids_;
    std::unordered_set<uint32_t> clock_ids_;

    struct Ready
    {
        uint64_t sim_time = 0;
        uint64_t window_id = 0;

        Ready(uint64_t sim_time, uint64_t window_id) :
            sim_time(sim_time),
            window_id(window_id)
        {
        }
    };

    std::queue<Ready> ready_queue_;
};

} // namespace simdb::argos
