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

template <typename T, typename = void> struct has_ostream_insertion : std::false_type
{
};

template <typename T>
struct has_ostream_insertion<T, std::void_t<decltype(std::declval<std::ostringstream&>() << std::declval<const T&>())>>
    : std::true_type
{
};

template <typename T> inline constexpr bool has_ostream_insertion_v = has_ostream_insertion<T>::value;

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
        dtype_nodes_tbl.addColumn("SpecialFormatters", dt::string_t);

        // TODO cnyce: floating-point timestamp support
        auto& timestamps_tbl = schema.addTable("Timestamps");
        timestamps_tbl.addColumn("Timestamp", dt::uint64_t);
        timestamps_tbl.ensureUnique("Timestamp");

        auto& collection_records_tbl = schema.addTable("CollectionRecords");
        collection_records_tbl.addColumn("TimestampID", dt::int32_t);
        collection_records_tbl.addColumn("Records", dt::blob_t);
        collection_records_tbl.unsetPrimaryKey();

        auto& queue_max_sizes_tbl = schema.addTable("QueueMaxSizes");
        queue_max_sizes_tbl.addColumn("SerializationCID", dt::int32_t);
        queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
        collectable_tns_tbl.ensureUnique("SerializationCID");
        collectable_tns_tbl.unsetPrimaryKey();
    }

    void setVerbose(bool verbose = true) { verbose_ = verbose; }

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

    void timestampWith(const uint64_t* backpointer)
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot change timestamp object after calling AppManagers::initializePipelines()");
        }
        timestamp_ = std::make_unique<Timestamp<uint64_t>>(backpointer);
    }

    void timestampWith(uint64_t (*fn)())
    {
        if (pipeline_stager_ != nullptr)
        {
            throw DBException("Cannot change timestamp object after calling AppManagers::initializePipelines()");
        }
        timestamp_ = std::make_unique<Timestamp<uint64_t>>(fn);
    }

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
        if (verbose_)
        {
            std::cout << "Collecting scalar:\n";
            std::cout << "  CID  - " << entry_point->getID() << "\n";
            std::cout << "  Path - " << path << "\n";
            std::cout << "  Type - " << entry_point->collectableTypeNameForDb() << std::endl;
        }
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
        if (verbose_)
        {
            std::cout << "Collecting container:\n";
            std::cout << "  CID  - " << entry_point->getID() << "\n";
            std::cout << "  Path - " << path << "\n";
            std::cout << "  Type - " << entry_point->collectableTypeNameForDb() << std::endl;
        }
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
        } else if constexpr (std::is_enum_v<T> && detail::has_ostream_insertion_v<T>)
        {
            // For now, stringifiable enums collect as strings
            return "string";
        } else if constexpr (std::is_enum_v<T>)
        {
            // Non-stringifiable enums collect as raw uint64_t values
            using Underlying = std::underlying_type_t<T>;
            static_assert(std::is_unsigned_v<Underlying>);
            return demangle_type<uint64_t>();
        } else if constexpr (type_traits::is_pair_v<T>)
        {
            using FirstT = typename T::first_type;
            auto first_type = encodeTypeName<FirstT>();

            using SecondT = typename T::second_type;
            auto second_type = encodeTypeName<SecondT>();

            return "pair(" + first_type + "," + second_type + ")";
        } else if constexpr (type_traits::is_stl_v<T>)
        {
            using ValueT = typename T::value_type;
            auto value_type = encodeTypeName<ValueT>();
            return "stl(" + value_type + ")";
        } else
        {
            return demangle_type<T>();
        }
    }

    TinyStrings<>* getTinyStrings() { return &tiny_strings_; }

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
        pipeline_stager_ = std::make_unique<PipelineStager<uint64_t>>(heartbeat_, timestamp_.get(), pipeline_head, notif_head);

        Schema notif_schema;
        using dt = SqlDataType;

        auto& notif_tbl = notif_schema.addTable("Notifications");
        notif_tbl.addColumn("SerializationCID", dt::int32_t);
        notif_tbl.addColumn("NotifStr", dt::string_t);
        notif_tbl.addColumn("NotifType", dt::int32_t);
        timestamp_->addTimeColumn(notif_tbl);
        db_mgr_->appendSchema(notif_schema);

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
            const auto dtype_name = collector->collectableTypeNameForDb();
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
        }

    private:
        pipeline::PipelineAction run_(bool) override
        {
            CompressedQueueCollectionData collection_at_time;
            if (input_queue_->try_pop(collection_at_time))
            {
                auto db_mgr = getDatabaseManager_();
                auto id = collection_at_time.time_point->createTimestampInDatabase(db_mgr);

                auto inserter = getTableInserter_("CollectionRecords");
                const auto& bytes = collection_at_time.compressed_collection_data;
                inserter->createRecordWithColValues(id, bytes);

                return pipeline::PipelineAction::PROCEED;
            }

            Notification notification;
            if (notif_queue_->try_pop(notification))
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

            return pipeline::PipelineAction::SLEEP;
        }

        ConcurrentQueue<CompressedQueueCollectionData>* input_queue_ = nullptr;
        ConcurrentQueue<Notification>* notif_queue_ = nullptr;
    };

    DatabaseManager* const db_mgr_;
    TinyStrings<> tiny_strings_{db_mgr_};
    size_t heartbeat_ = DEFAULT_HEARTBEAT;
    bool verbose_ = false;

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
