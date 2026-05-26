// <ArgosCollector.hpp> -*- C++ *-*

#pragma once

#include "simdb/apps/App.hpp"
#include "simdb/apps/argos/Collectables.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/utils/Compress.hpp"
#include "simdb/utils/TypeTraits.hpp"

namespace simdb::argos {

inline constexpr size_t DEFAULT_HEARTBEAT = 10;

namespace detail {

// TODO cnyce: reuse Sparta's has_ostream_operator utility once it gets moved to SimDB.
template <typename T, typename = void> struct has_ostream_operator : std::false_type
{
};

template <typename T>
struct has_ostream_operator<T, std::void_t<decltype(std::declval<std::ostringstream&>() << std::declval<const T&>())>>
    : std::true_type
{
};

template <typename T> inline constexpr bool has_ostream_operator_v = has_ostream_operator<T>::value;

template <typename T, typename = void> struct has_sparta_pair_definition_type : std::false_type
{
};

template <typename T>
struct has_sparta_pair_definition_type<T, std::void_t<typename T::SpartaPairDefinitionType>> : std::true_type
{
};

template <typename T>
inline constexpr bool has_sparta_pair_definition_type_v = has_sparta_pair_definition_type<T>::value;

template <typename T> struct is_dynamic_type : std::false_type
{
};

template <typename T> inline constexpr bool is_dynamic_type_v = is_dynamic_type<T>::value;

} // namespace detail

class ArgosCollector : public App
{
public:
    static constexpr auto NAME = "argos-collector";

    ArgosCollector(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
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
        collectable_tns_tbl.ensureUnique("SerializationCID");
        collectable_tns_tbl.unsetPrimaryKey();

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_schemas_tbl = schema.addTable("DataTypeSchemas");
        dtype_schemas_tbl.addColumn("RootTypeName", dt::string_t);

        // TODO cnyce: populate this table in SimDB (Sparta will handle it for now)
        auto& dtype_nodes_tbl = schema.addTable("DataTypeNodes");
        dtype_nodes_tbl.addColumn("SchemaId", dt::int32_t);
        dtype_nodes_tbl.addColumn("ParentId", dt::int32_t);
        dtype_nodes_tbl.addColumn("Kind", dt::string_t);
        dtype_nodes_tbl.addColumn("Name", dt::string_t);
        dtype_nodes_tbl.addColumn("Description", dt::string_t);
        dtype_nodes_tbl.addColumn("TypeName", dt::string_t);
        dtype_nodes_tbl.addColumn("EnumBacking", dt::string_t);
        dtype_nodes_tbl.addColumn("FormatStr", dt::string_t);

        // TODO cnyce: floating-point timestamp support
        auto& timestamps_tbl = schema.addTable("Timestamps");
        timestamps_tbl.addColumn("Timestamp", dt::uint64_t);
        timestamps_tbl.ensureUnique("Timestamp");

        auto& collection_records_tbl = schema.addTable("CollectionRecords");
        collection_records_tbl.addColumn("TimestampID", dt::int32_t);
        collection_records_tbl.addColumn("Records", dt::blob_t);
        collection_records_tbl.ensureUnique("TimestampID");
        collection_records_tbl.unsetPrimaryKey();

        auto& queue_max_sizes_tbl = schema.addTable("QueueMaxSizes");
        queue_max_sizes_tbl.addColumn("SerializationCID", dt::int32_t);
        queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
        collectable_tns_tbl.ensureUnique("SerializationCID");
        collectable_tns_tbl.unsetPrimaryKey();

        // TODO cnyce: floating-point timestamp support
        auto& notif_tbl = schema.addTable("Notifications");
        notif_tbl.addColumn("SerializationCID", dt::int32_t);
        notif_tbl.addColumn("NotifStr", dt::string_t);
        notif_tbl.addColumn("NotifType", dt::int32_t);
        notif_tbl.addColumn("Timestamp", dt::uint64_t);

        // TODO cnyce: floating-point timestamp support
        auto& dyn_field_changes_tbl = schema.addTable("DynamicFieldChanges");
        dyn_field_changes_tbl.addColumn("SerializationCID", dt::int32_t);
        dyn_field_changes_tbl.addColumn("FieldNames", dt::string_t);
        dyn_field_changes_tbl.addColumn("FieldTypes", dt::string_t);
        dyn_field_changes_tbl.addColumn("Timestamp", dt::uint64_t);
    }

    void setHeartbeat(size_t heartbeat)
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot reset heartbeat once pipeline is created");
        }
        if (heartbeat == 0)
        {
            throw DBException("Cannot use 0 for Argos collector heartbeat");
        }
        heartbeat_ = heartbeat;
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

    // TODO cnyce: floating-point timestamp support
    void timestampWith(const uint64_t* backpointer)
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot change timestamp object after calling AppManagers::initializePipelines()");
        }
        timestamp_ = std::make_unique<Timestamp<uint64_t>>(backpointer);
    }

    // TODO cnyce: floating-point timestamp support
    void timestampWith(uint64_t (*fn)())
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot change timestamp object after calling AppManagers::initializePipelines()");
        }
        timestamp_ = std::make_unique<Timestamp<uint64_t>>(fn);
    }

    // TODO cnyce: floating-point timestamp support
    void timestampWith(std::function<uint64_t()> fn)
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot change timestamp object after calling AppManagers::initializePipelines()");
        }
        timestamp_ = std::make_unique<Timestamp<uint64_t>>(fn);
    }

    template <typename ScalarT>
    CollectionEntryPoint* createScalarCollector(const std::string& path, const std::string& clk_name)
    {
        auto entry_point = std::make_unique<CollectionEntryPoint>(heartbeat_, &tiny_strings_);
        entry_point->setScalarDataType(encodeTypeName<ScalarT>());
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name);
        collectors_.emplace_back(std::move(entry_point));
        return static_cast<CollectionEntryPoint*>(collectors_.back().get());
    }

    template <typename BinT, bool Sparse>
    CollectionEntryPoint* createContainerCollector(const std::string& path, const std::string& clk_name,
                                                   size_t capacity)
    {
        auto entry_point = std::make_unique<CollectionEntryPoint>(heartbeat_, &tiny_strings_);
        entry_point->setContainerDataType(encodeTypeName<BinT>(), Sparse, capacity);
        meta_by_cid_[entry_point->getID()] = std::make_tuple(path, clk_name);
        collectors_.emplace_back(std::move(entry_point));
        return static_cast<CollectionEntryPoint*>(collectors_.back().get());
    }

    template <typename T> static std::string encodeTypeName()
    {
        if constexpr (std::is_same_v<T, std::string>)
        {
            return "string";
        } else if constexpr (std::is_same_v<std::decay_t<T>, const char*>)
        {
            return "string";
        } else if constexpr (std::is_enum_v<T> && detail::has_ostream_operator_v<T>)
        {
            // TODO cnyce: For now, stringifiable enums collect as strings
            return "string";
        } else if constexpr (std::is_enum_v<T>)
        {
            // Non-stringifiable enums collect as raw uint64_t values
            using Underlying = std::underlying_type_t<T>;
            static_assert(std::is_unsigned_v<Underlying>);
            return demangle_type<uint64_t>();
        } else if constexpr (type_traits::is_pod_v<T> || detail::has_sparta_pair_definition_type_v<T>)
        {
            return demangle_type<T>();
        } else if constexpr (detail::is_dynamic_type_v<T>)
        {
            return "dynamic";
        } else
        {
            static_assert(detail::has_ostream_operator_v<T>);
            return "string";
        }
    }

    TinyStrings<>* getTinyStrings() { return &tiny_strings_; }

    PipelineStagerBase* getStager() const { return pipeline_stager_.get(); }

    void createPipeline(pipeline::PipelineManager* pipeline_mgr) override
    {
        if (timestamp_ == nullptr)
        {
            throw DBException("You must call timestampWith() before calling AppManagers::initializePipelines()");
        }

        auto pipeline = pipeline_mgr->createPipeline(NAME, this);

        pipeline->addStage<Compressor>("compressor");
        pipeline->addStage<Writer>("writer");
        pipeline->noMoreStages();

        pipeline->bind("compressor.output_queue", "writer.input_queue");
        pipeline->noMoreBindings();

        auto pipeline_head = pipeline->getInPortQueue<QueueCollectionData>("compressor.input_queue");
        auto notif_head = pipeline->getInPortQueue<Notification>("writer.notif_queue");
        auto dyn_field_head = pipeline->getInPortQueue<DynamicFieldChanges>("writer.dyn_field_queue");
        pipeline_stager_ =
            std::make_unique<PipelineStager<uint64_t>>(heartbeat_, timestamp_.get(), pipeline_head, notif_head, dyn_field_head);

        for (const auto& collector : collectors_)
        {
            collector->connectToPipeline(pipeline_stager_.get());
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
            const auto dtype_name = collector->encodeTypeName();
            ctn_inserter->createRecordWithColValues(cid, full_path, clk_id, dtype_name);
        }
    }

    void sendCollectedDataToPipeline()
    {
        if (!pipeline_stager_) [[unlikely]]
        {
            throw DBException("PipelineStager never set!");
        }
        pipeline_stager_->sendCollectedDataToPipeline();
    }

    void preTeardown() override
    {
        if (pipeline_stager_)
        {
            sendCollectedDataToPipeline();
        }
    }

    void postTeardown() override
    {
        for (auto& collector : collectors_)
        {
            collector->writeMetaOnPostTeardown(db_mgr_);
        }
        tiny_strings_.serialize();
    }

private:
    struct CompressedQueueCollectionData
    {
        CollectionTime time_point;
        std::vector<char> compressed_collection_data;
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
                for (const auto& src : collection_at_time.collection_data)
                {
                    const auto& src_data = src->getData();
                    uncompressed.insert(uncompressed.end(), src_data.begin(), src_data.end());
                }

                CompressedQueueCollectionData compressed;
                compressData(uncompressed, compressed.compressed_collection_data);
                compressed.time_point = collection_at_time.time_point;
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
                auto id = collection_at_time.time_point->createTimestampInDatabase(db_mgr);

                auto inserter = getTableInserter_("CollectionRecords");
                const auto& bytes = collection_at_time.compressed_collection_data;
                inserter->createRecordWithColValues(id, bytes);

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
                assert(cid > 0);

                inserter->setColumnValue(0, (int)cid);
                inserter->setColumnValue(1, notif_str);
                inserter->setColumnValue(2, (int)notif_type);
                if (notification.time_point)
                {
                    notification.time_point->assign(inserter, 3);
                }
                inserter->createRecord();
            }

            DynamicFieldChanges dyn_field_changes;
            while (dyn_field_queue_->try_pop(dyn_field_changes))
            {
                // TODO cnyce, TODO XXX:
                // We need a separate table for the CID dynamic field names
                // and not pile them into the same table as the field types

                const auto cid = dyn_field_changes.cid;
                const auto& field_names = dyn_field_changes.field_names;
                const auto& field_types = dyn_field_changes.field_types;
                assert(field_names.size() == field_types.size());
                assert(!field_names.empty());
                assert(cid > 0);

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

                comma = false;
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

                auto inserter = getTableInserter_("DynamicFieldChanges");
                inserter->setColumnValue(0, (int)cid);
                inserter->setColumnValue(1, concat_field_names.str());
                inserter->setColumnValue(2, concat_field_types.str());

                assert(dyn_field_changes.time_point != nullptr);
                dyn_field_changes.time_point->assign(inserter, 3);
                inserter->createRecord();

                action = pipeline::PipelineAction::PROCEED;
            }

            return action;
        }

        ConcurrentQueue<CompressedQueueCollectionData>* input_queue_ = nullptr;
        ConcurrentQueue<Notification>* notif_queue_ = nullptr;
        ConcurrentQueue<DynamicFieldChanges>* dyn_field_queue_ = nullptr;
    };

    DatabaseManager* const db_mgr_;
    TinyStrings<> tiny_strings_{db_mgr_};
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

    std::unique_ptr<Timestamp<uint64_t>> timestamp_;
    std::unique_ptr<PipelineStager<uint64_t>> pipeline_stager_;
    std::vector<std::unique_ptr<CollectableBase>> collectors_;
};

} // namespace simdb::argos
