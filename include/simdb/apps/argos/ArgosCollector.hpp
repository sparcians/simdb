// <ArgosCollector.hpp> -*- C++ *-*

#pragma once

#include "simdb/apps/App.hpp"
#include "simdb/apps/argos/ArgosResources.hpp"
#include "simdb/apps/argos/CheckpointerAsyncEncode.hpp"
#include "simdb/apps/argos/Collectables.hpp"
#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/pipeline/Flusher.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/sqlite/Dump.hpp"
#include "simdb/utils/Compress.hpp"
#include "simdb/utils/SafeWeakPtr.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

inline constexpr size_t DEFAULT_HEARTBEAT = 10;

//! \class ArgosCollector
//! \brief Main entry point into the Argos collection system.
class ArgosCollector : public App
{
public:
    //! Required by all SimDB apps
    static constexpr auto NAME = "argos-collector";

    ArgosCollector(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
        resources_.setDatabase(db_mgr);
        resources_.setHeartbeat(DEFAULT_HEARTBEAT);
    }

    static void defineSchema(Schema& schema)
    {
        using dt = SqlDataType;

        auto& globals_tbl = schema.addTable("CollectionGlobals");
        globals_tbl.addColumn("Heartbeat", dt::int32_t);

        auto& clks_tbl = schema.addTable("Clocks");
        clks_tbl.addColumn("Name", dt::string_t);
        clks_tbl.addColumn("Period", dt::uint32_t);
        clks_tbl.addColumn("Numer", dt::uint32_t);
        clks_tbl.addColumn("Denom", dt::uint32_t);
        clks_tbl.setColumnDefaultValue("Numer", 0);
        clks_tbl.setColumnDefaultValue("Denom", 0);

        auto& collectable_tns_tbl = schema.addTable("CollectableTreeNodes");
        collectable_tns_tbl.addColumn("SerializationCID", dt::int32_t);
        collectable_tns_tbl.addColumn("FullPath", dt::string_t);
        collectable_tns_tbl.addColumn("ClockID", dt::int32_t);
        collectable_tns_tbl.addColumn("TypeName", dt::string_t);
        collectable_tns_tbl.addColumn("ShowInUI", dt::int32_t);
        collectable_tns_tbl.setColumnDefaultValue("ShowInUI", 0);
        collectable_tns_tbl.ensureUnique("SerializationCID");
        collectable_tns_tbl.createIndexOn("SerializationCID");
        collectable_tns_tbl.unsetPrimaryKey();

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_schemas_tbl = schema.addTable("DataTypeSchemas");
        dtype_schemas_tbl.addColumn("RootTypeName", dt::string_t);

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_nodes_tbl = schema.addTable("DataTypeNodes");
        dtype_nodes_tbl.addColumn("SchemaId", dt::int32_t);
        dtype_nodes_tbl.addColumn("Name", dt::string_t);
        dtype_nodes_tbl.addColumn("TypeName", dt::string_t);
        dtype_nodes_tbl.addColumn("FormatStr", dt::string_t);

        auto& signed_enum_map_tbl = schema.addTable("SignedEnumMappings");
        signed_enum_map_tbl.addColumn("EnumName", dt::string_t);
        signed_enum_map_tbl.addColumn("EnumString", dt::string_t);
        signed_enum_map_tbl.addColumn("EnumInt", dt::int64_t);

        auto& unsigned_enum_map_tbl = schema.addTable("UnsignedEnumMappings");
        unsigned_enum_map_tbl.addColumn("EnumName", dt::string_t);
        unsigned_enum_map_tbl.addColumn("EnumString", dt::string_t);
        unsigned_enum_map_tbl.addColumn("EnumInt", dt::uint64_t);

        auto& timestamps_tbl = schema.addTable("Timestamps");
        timestamps_tbl.addColumn("Timestamp", dt::uint64_t);
        timestamps_tbl.ensureUnique("Timestamp");

        auto& collection_records_tbl = schema.addTable("CollectionRecords");
        collection_records_tbl.addColumn("TimestampID", dt::int32_t);
        collection_records_tbl.addColumn("Records", dt::blob_t);
        collection_records_tbl.ensureUnique("TimestampID");
        collection_records_tbl.unsetPrimaryKey();

        auto& timestamp_clocks_tbl = schema.addTable("TimestampClocks");
        timestamp_clocks_tbl.addColumn("TimestampID", dt::int32_t);
        timestamp_clocks_tbl.addColumn("ClockID", dt::int32_t);
        timestamp_clocks_tbl.createCompoundIndexOn({"TimestampID", "ClockID"});
        timestamp_clocks_tbl.unsetPrimaryKey();

        auto& queue_max_sizes_tbl = schema.addTable("QueueMaxSizes");
        queue_max_sizes_tbl.addColumn("SerializationCID", dt::int32_t);
        queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
        queue_max_sizes_tbl.ensureUnique("SerializationCID");
        queue_max_sizes_tbl.unsetPrimaryKey();

        auto& notif_tbl = schema.addTable("Notifications");
        notif_tbl.addColumn("SerializationCID", dt::int32_t);
        notif_tbl.addColumn("Timestamp", dt::uint64_t);
        notif_tbl.addColumn("NotifType", dt::int32_t);
        notif_tbl.addColumn("NotifStr", dt::string_t);

        auto& dyn_field_type_changes_tbl = schema.addTable("DynamicFieldTypeChanges");
        dyn_field_type_changes_tbl.addColumn("SerializationCID", dt::int32_t);
        dyn_field_type_changes_tbl.addColumn("FieldTypes", dt::string_t);
        dyn_field_type_changes_tbl.addColumn("Timestamp", dt::uint64_t);
        dyn_field_type_changes_tbl.createCompoundIndexOn({"SerializationCID", "Timestamp"});

        auto& dyn_field_names_tbl = schema.addTable("DynamicFieldNames");
        dyn_field_names_tbl.addColumn("SerializationCID", dt::int32_t);
        dyn_field_names_tbl.addColumn("FieldNames", dt::string_t);
        dyn_field_names_tbl.createIndexOn("SerializationCID");
    }

    void setHeartbeat(size_t heartbeat)
    {
        if (heartbeat == 0)
        {
            throw DBException("Cannot use 0 for Argos collector heartbeat");
        }
        heartbeat_ = heartbeat;
        resources_.setHeartbeat(heartbeat);
    }

    void addClock(const std::string& clk_name, size_t period) { addClock(clk_name, period, 0, 0); }

    void addClock(const std::string& clk_name, size_t period, size_t numer, size_t denom)
    {
        for (const auto& [_clk_name, _period, _numer, _denom] : clocks_)
        {
            if (clk_name == _clk_name)
            {
                if (period != _period || numer != _numer || denom != _denom)
                {
                    throw DBException("Clock mismatch - already registered with different params: ") << clk_name;
                }
            }
        }

        auto clk_desc = std::make_tuple(clk_name, period, numer, denom);
        clocks_.emplace_back(std::move(clk_desc));
    }

    void timestampWith(const uint64_t* backpointer)
    {
        if (timestamp_ != nullptr)
        {
            throw DBException("Cannot change timestamp object once created!");
        }
        timestamp_ = std::make_unique<Timestamp>(backpointer);
        resources_.setTimestamp(timestamp_.get());
    }

    void timestampWith(uint64_t (*fn)())
    {
        if (timestamp_ != nullptr)
        {
            throw DBException("Cannot change timestamp object once created!");
        }
        timestamp_ = std::make_unique<Timestamp>(fn);
        resources_.setTimestamp(timestamp_.get());
    }

    void timestampWith(std::function<uint64_t()> fn)
    {
        if (timestamp_ != nullptr)
        {
            throw DBException("Cannot change timestamp object once created!");
        }
        timestamp_ = std::make_unique<Timestamp>(fn);
        resources_.setTimestamp(timestamp_.get());
    }

    //! TODO cnyce: Once the collection code from Sparta is moved to SimDB, change this
    //! to a template method so we can figure out the encoded data type name ourselves.
    //! Scalar types are encoded as follows:
    //!
    //!   For scalar PODs:
    //!   typeid(T).name()
    //!       "bool"
    //!       "unsigned long"
    //!       ...
    //!
    //!   For scalar enums with operator<< (defn held in separate table for string-int map):
    //!   typeid(T).name()
    //!       "IssueType"
    //!       "MMUState"
    //!       ...
    //!
    //!   For scalar enums without operator<< (treated just like int PODs):
    //!   typeid(std::underlying_type_t<T>).name()
    //!       "int"
    //!       "unsigned int"
    //!       ...
    //!
    //!   For scalar struct-like types:
    //!   typeid(T).name()
    //!       "Packet"
    //!       "Inst"
    //!       ...
    //!
    //!   For scalar string-like types (std::string, const char*):
    //!       "string"
    CollectionEntryPoint* createScalarCollector(const std::string& path, const std::string& clk_name,
                                                const std::string& encoded_scalar_type)
    {
        auto entry_point = std::make_unique<CollectionEntryPoint>(&resources_);
        entry_point->setScalarDataType(encoded_scalar_type);
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name);
        collectors_.emplace_back(std::move(entry_point));
        return collectors_.back().get();
    }

    //! TODO cnyce: Once the collection code from Sparta is moved to SimDB, change this
    //! to a template method so we can figure out the encoded data type name ourselves.
    //! Container types are encoded as follows:
    //!
    //!   <encoded_scalar_type>_<sparse/contig>_capacity<N>
    //!       "Inst_sparse_capacity32"
    //!       "bool_contig_capacity4"
    //!       ...
    CollectionEntryPoint* createContainerCollector(const std::string& path, const std::string& clk_name,
                                                   const std::string& encoded_container_type)
    {
        auto entry_point = std::make_unique<CollectionEntryPoint>(&resources_);
        entry_point->setContainerDataType(encoded_container_type);
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name);
        collectors_.emplace_back(std::move(entry_point));
        return collectors_.back().get();
    }

    safe_weak_ptr<TinyStrings<>> getTinyStrings() { return resources_.getTinyStringsResource().get(); }

    safe_weak_ptr<PipelineStager> getStager() { return resources_.getStagerResource().get(); }

    ArgosResources* getResources() { return &resources_; }

    void enableAsyncEncoding(bool enable = true) { resources_.getStagerResource().enableAsyncEncoding(enable); }

    bool asyncEncodingEnabled() const { return resources_.getStagerResource().asyncEncodingEnabled(); }

    void createPipeline(pipeline::PipelineManager* pipeline_mgr) override
    {
        auto pipeline = pipeline_mgr->createPipeline(NAME, this);
        const bool async_enc = asyncEncodingEnabled();

        if (async_enc)
        {
            pipeline->addStage<EncoderStage>("encoder", async_wire_sent_cids_, &async_encode_completion_queue_);
        }
        pipeline->addStage<Compressor>("compressor");
        pipeline->addStage<Writer>("writer");
        pipeline->noMoreStages();

        if (async_enc)
        {
            pipeline->bind("encoder.output_queue", "compressor.input_queue");
        }
        pipeline->bind("compressor.output_queue", "writer.input_queue");
        pipeline->noMoreBindings();

        if (async_enc)
        {
            resources_.getStagerResource().setAsyncWireSentCids(&async_wire_sent_cids_);
            resources_.getStagerResource().setAsyncEncodeCompletionQueue(&async_encode_completion_queue_);
        }

        resources_.setPipeline(pipeline);

        if (async_enc)
        {
            pipeline_flusher_ = pipeline->createFlusher({"encoder", "compressor", "writer"});
        } else
        {
            pipeline_flusher_ = pipeline->createFlusher({"compressor", "writer"});
        }
    }

    void postInit(int, char**) override
    {
        db_mgr_->INSERT(SQL_TABLE("CollectionGlobals"), SQL_VALUES(heartbeat_));

        std::map<std::string, int> clk_ids;
        auto clk_inserter = db_mgr_->prepareINSERT(SQL_TABLE("Clocks"));
        for (const auto& [_clk_name, _period, _numer, _denom] : clocks_)
        {
            auto id = clk_inserter->createRecordWithColValues(_clk_name, _period, _numer, _denom);
            clk_ids[_clk_name] = id;
        }

        auto ctn_inserter = db_mgr_->prepareINSERT(SQL_TABLE("CollectableTreeNodes"));
        for (const auto& collector : collectors_)
        {
            auto cid = (int)collector->getID();
            const auto& full_path = std::get<0>(meta_by_cid_.at(cid));
            const auto& clk_name = std::get<1>(meta_by_cid_.at(cid));
            const auto clk_id = clk_ids.at(clk_name);
            const auto dtype_name = collector->getEncodedCollectedType();
            ctn_inserter->createRecordWithColValues(cid, full_path, clk_id, dtype_name);
            pipeline_stager_->setCollectableClock(collector->getID(), getClockId_(clk_name));
        }
    }

    void preTeardown() override
    {
        pipeline_stager_->sendCollectedDataToPipeline();
        if (pipeline_flusher_)
        {
            pipeline_flusher_->flush();
        }
        pipeline_stager_->drainAsyncEncodeCompletions();
    }

    void postTeardown() override
    {
        resources_.writeMetaOnPostTeardown(db_mgr_);

        if (verbose())
        {
            std::cout << "[simdb] Collection tables at the end of simulation (except timestamps/blobs):\n\n";
            dumpTable(db_mgr_, "CollectionGlobals");
            dumpTable(db_mgr_, "Clocks");
            dumpTable(db_mgr_, "CollectableTreeNodes");
            dumpTable(db_mgr_, "DataTypeSchemas");
            dumpTable(db_mgr_, "DataTypeNodes");
            dumpTable(db_mgr_, "SignedEnumMappings");
            dumpTable(db_mgr_, "UnsignedEnumMappings");
            dumpTable(db_mgr_, "QueueMaxSizes");
            dumpTable(db_mgr_, "Notifications");
            dumpTable(db_mgr_, "DynamicFieldTypeChanges");
        }
    }

private:
    class EncoderStage : public pipeline::Stage
    {
    public:
        EncoderStage(std::unordered_set<uint16_t>& wire_sent_cids,
                     ConcurrentQueue<AsyncEncodeCompletion>* completion_queue) :
            wire_sent_cids_(wire_sent_cids),
            completion_queue_(completion_queue)
        {
            addInPort_<DeltaEncodingBatch>("input_queue", input_queue_);
            addOutPort_<QueueCollectionData>("output_queue", output_queue_);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            DeltaEncodingBatch batch;
            if (!input_queue_->try_pop(batch))
            {
                return pipeline::PipelineAction::SLEEP;
            }

            QueueCollectionData to_send;
            to_send.sim_time = batch.sim_time;

            for (const auto& work : batch.work)
            {
                auto& wire_state = wire_state_by_cid_[work.cid];
                auto result = encodeCidWork(work, batch.sim_time, wire_state);
                if (result.committed_active_anchor && work.stolen_chain_tail && completion_queue_ != nullptr)
                {
                    AsyncEncodeCompletion completion;
                    completion.cid = work.cid;
                    completion.tail = work.stolen_chain_tail;
                    completion.release_through_full_anchor = result.release_through_full_anchor;
                    completion_queue_->emplace(std::move(completion));
                }
                for (auto& entry : result.wires)
                {
                    wire_sent_cids_.insert(work.cid);
                    to_send.entries.emplace_back(std::move(entry));
                    if (const auto clk_it = batch.cid_to_clock.find(work.cid); clk_it != batch.cid_to_clock.end())
                    {
                        to_send.clock_ids.insert(clk_it->second);
                    }
                }
            }

            output_queue_->emplace(std::move(to_send));
            return pipeline::PipelineAction::PROCEED;
        }

        ConcurrentQueue<DeltaEncodingBatch>* input_queue_ = nullptr;
        ConcurrentQueue<QueueCollectionData>* output_queue_ = nullptr;
        std::unordered_map<uint16_t, WireAccountingState> wire_state_by_cid_;
        std::unordered_set<uint16_t>& wire_sent_cids_;
        ConcurrentQueue<AsyncEncodeCompletion>* completion_queue_ = nullptr;
    };

    class Compressor : public pipeline::Stage
    {
    public:
        Compressor()
        {
            addInPort_<QueueCollectionData>("input_queue", input_queue_);
            addOutPort_<CompressedQueueCollectionData>("output_queue", output_queue_);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            QueueCollectionData collection_at_time;
            if (input_queue_->try_pop(collection_at_time))
            {
                std::vector<char> uncompressed;
                for (const auto& src : collection_at_time.entries)
                {
                    const auto& src_data = src->getData();
                    uncompressed.insert(uncompressed.end(), src_data.begin(), src_data.end());
                }

                CompressedQueueCollectionData compressed;
                compressData(uncompressed, compressed.compressed_collection_data);
                compressed.sim_time = collection_at_time.sim_time;
                compressed.clock_ids = collection_at_time.clock_ids;
                output_queue_->emplace(std::move(compressed));
                return pipeline::PipelineAction::PROCEED;
            }

            return pipeline::PipelineAction::SLEEP;
        }

        ConcurrentQueue<QueueCollectionData>* input_queue_ = nullptr;
        ConcurrentQueue<CompressedQueueCollectionData>* output_queue_ = nullptr;
    };

    class Writer : public pipeline::DatabaseStage<ArgosCollector>
    {
    public:
        Writer()
        {
            addInPort_<CompressedQueueCollectionData>("input_queue", input_queue_);
            addInPort_<Notification>("notif_queue", notif_queue_);
            addInPort_<DynamicFieldChanges>("dyn_field_queue", dyn_field_queue_);
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            // If anything is ready for collection, process one of them and PROCEED.
            auto action = pipeline::PipelineAction::SLEEP;

            CompressedQueueCollectionData collection_at_time;
            if (input_queue_->try_pop(collection_at_time))
            {
                auto db_mgr = getDatabaseManager_();
                auto id = Timestamp::createTimestampInDatabase(db_mgr, collection_at_time.sim_time);

                auto inserter = getTableInserter_("CollectionRecords");
                const auto& bytes = collection_at_time.compressed_collection_data;
                inserter->createRecordWithColValues(id, bytes);

                if (!collection_at_time.clock_ids.empty())
                {
                    auto clk_inserter = getTableInserter_("TimestampClocks");
                    for (const auto clock_id : collection_at_time.clock_ids)
                    {
                        clk_inserter->createRecordWithColValues(id, static_cast<int>(clock_id));
                    }
                }

                action = pipeline::PipelineAction::PROCEED;
            }

            // Notifications are expected to be "sporadic" / one-off. Flush all
            // of the notifs now, and do not consider "notification-only" to mean
            // "keep greedily processing / keep threads running". This is the
            // reason why we use WHILE here instead of IF like above, and the
            // returned action is not set to PROCEED based on the availability
            // of new notifications.
            Notification notification;
            while (notif_queue_->try_pop(notification))
            {
                auto inserter = getTableInserter_("Notifications");

                const auto cid = notification.cid;
                const auto& notif_str = notification.notif;
                const auto notif_type = notification.type;

                inserter->setColumnValue(0, (int)cid);
                if (notification.sim_time.isValid())
                {
                    inserter->setColumnValue(1, notification.sim_time.getValue());
                }
                inserter->setColumnValue(2, (int)notif_type);
                inserter->setColumnValue(3, notif_str);
                inserter->createRecord();
            }

            DynamicFieldChanges dyn_field_changes;
            while (dyn_field_queue_->try_pop(dyn_field_changes))
            {
                const auto cid = dyn_field_changes.cid;
                const auto& field_names = dyn_field_changes.field_names;
                const auto& field_types = dyn_field_changes.field_types;
                assert(field_names.size() == field_types.size());
                assert(!field_names.empty());
                assert(cid != 0);

                if (serialized_dyn_field_cids_.insert(cid).second)
                {
                    bool comma = false;
                    std::ostringstream concat_field_names;
                    for (const auto& field_name : field_names)
                    {
                        if (comma)
                        {
                            concat_field_names << ",";
                        }
                        comma = true;
                        concat_field_names << field_name;
                    }

                    auto inserter = getTableInserter_("DynamicFieldNames");
                    inserter->createRecordWithColValues((int)cid, concat_field_names.str());
                }

                bool comma = false;
                std::ostringstream concat_field_types;
                for (const auto field_type : field_types)
                {
                    if (comma)
                    {
                        concat_field_types << ",";
                    }
                    comma = true;
                    concat_field_types << field_type;
                }

                auto inserter = getTableInserter_("DynamicFieldTypeChanges");
                inserter->createRecordWithColValues((int)cid, concat_field_types.str(), dyn_field_changes.sim_time);

                action = pipeline::PipelineAction::PROCEED;
            }

            return action;
        }

        ConcurrentQueue<CompressedQueueCollectionData>* input_queue_ = nullptr;
        ConcurrentQueue<Notification>* notif_queue_ = nullptr;
        ConcurrentQueue<DynamicFieldChanges>* dyn_field_queue_ = nullptr;
        std::unordered_set<uint16_t> serialized_dyn_field_cids_;
    };

    DatabaseManager* const db_mgr_;
    size_t heartbeat_ = DEFAULT_HEARTBEAT;

    using ClockDescriptor = std::tuple<std::string, // clk name
                                       uint32_t,    // period
                                       uint32_t,    // numerator
                                       uint32_t     // denominator
                                       >;
    std::vector<ClockDescriptor> clocks_;

    using CollectableMeta = std::tuple<std::string, // dot-delimited path
                                       std::string  // clk name
                                       >;
    std::map<uint16_t, CollectableMeta> meta_by_cid_;

    uint32_t getClockId_(const std::string& clk_name) const
    {
        for (size_t i = 0; i < clocks_.size(); ++i)
        {
            if (std::get<0>(clocks_[i]) == clk_name)
            {
                return static_cast<uint32_t>(i + 1);
            }
        }
        throw DBException("Unknown clock: ") << clk_name;
    }

    std::unique_ptr<Timestamp> timestamp_;
    std::vector<std::unique_ptr<CollectionEntryPoint>> collectors_;
    ArgosResources resources_;
    PipelineStagerResource& pipeline_stager_{resources_.getStagerResource()};
    std::unordered_set<uint16_t> async_wire_sent_cids_;
    ConcurrentQueue<AsyncEncodeCompletion> async_encode_completion_queue_;
    std::unique_ptr<pipeline::Flusher> pipeline_flusher_;
};

} // namespace simdb::argos
