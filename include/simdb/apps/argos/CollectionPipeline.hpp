// <CollectionPipeline.hpp> -*- C++ *-*

#pragma once

#include "simdb/apps/App.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/apps/argos/CollectionBase.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/Compress.hpp"

namespace simdb::collection {

/// \class CollectionPipeline
/// \brief SimDB \ref App that registers collection metadata schema and forwards lifecycle events to a \ref CollectionPipelineMeta handler.
///
/// Owns a \ref TinyStrings instance backed by \a db_mgr. The pipeline name is \ref NAME for factory lookup.
class CollectionPipeline : public App
{
public:
    static constexpr auto NAME = "collection-pipeline";

    /// \brief Construct the collection pipeline app.
    /// \param db_mgr Database manager used for schema and persistence.
    /// \param handler Non-owning callbacks; must remain valid for the lifetime of this app (typically supplied via \ref AppFactory::parameterize).
    CollectionPipeline(DatabaseManager* db_mgr, CollectionBase* collection)
        : db_mgr_(db_mgr)
        , collection_(collection)
    {}

    /// \class AppFactory
    /// \brief \ref AppFactoryBase that creates \ref CollectionPipeline instances sharing a configured \ref CollectionPipelineMeta.
    class AppFactory : public AppFactoryBase
    {
    public:
        using AppT = CollectionPipeline;

        /// \brief Forward the collection to the pipeline app.
        void parameterize(CollectionBase* collection)
        {
            collection_ = collection;
        }

        /// \brief Allocate a new pipeline app.
        /// \param db_mgr Database manager for the new app.
        /// \return New \ref CollectionPipeline; ownership follows the app framework contract.
        AppT* createApp(DatabaseManager* db_mgr) override
        {
            assert(collection_ != nullptr);
            return new AppT(db_mgr, collection_);
        }

        /// \brief Populate \a schema with the same tables as \ref CollectionPipeline::defineSchema.
        /// \param schema Schema to fill before opening the database.
        void defineSchema(Schema& schema) const override
        {
            AppT::defineSchema(schema);
            auto& timestamps_tbl = schema.addTable("Timestamps");
            timestamps_tbl.addColumn("Timestamp", collection_->getSqlTimeType());
            timestamps_tbl.ensureUnique("Timestamp");
        }

    private:
        CollectionBase* collection_ = nullptr;
    };

    /// \brief Declare SQLite tables used by Argos collection (globals, clocks, element/collectable trees,
    /// data-type metadata, string map, records, queue sizes).
    /// \param schema Schema object to extend.
    static void defineSchema(Schema& schema)
    {
        using dt = SqlDataType;

        auto& globals_tbl = schema.addTable("CollectionGlobals");
        globals_tbl.addColumn("Heartbeat", dt::int32_t);

        auto& dtype_schemas_tbl = schema.addTable("DataTypeSchemas");
        dtype_schemas_tbl.addColumn("RootTypeName", dt::string_t);
        dtype_schemas_tbl.addColumn("EffectiveColorKey", dt::string_t);

        auto& dtype_nodes_tbl = schema.addTable("DataTypeNodes");
        dtype_nodes_tbl.addColumn("SchemaId", dt::int32_t);
        dtype_nodes_tbl.addColumn("ParentId", dt::int32_t);
        dtype_nodes_tbl.addColumn("Kind", dt::string_t);
        dtype_nodes_tbl.addColumn("Name", dt::string_t);
        dtype_nodes_tbl.addColumn("Description", dt::string_t);
        dtype_nodes_tbl.addColumn("TypeName", dt::string_t);
        dtype_nodes_tbl.addColumn("EnumBacking", dt::string_t);
        dtype_nodes_tbl.addColumn("SpecialFormatters", dt::string_t);

        auto& dtype_enum_members_tbl = schema.addTable("DataTypeEnumMembers");
        dtype_enum_members_tbl.addColumn("EnumNodeId", dt::int32_t);
        dtype_enum_members_tbl.addColumn("MemberName", dt::string_t);
        dtype_enum_members_tbl.addColumn("MemberValue", dt::string_t);

        auto& clks_tbl = schema.addTable("Clocks");
        clks_tbl.addColumn("Name", dt::string_t);
        clks_tbl.addColumn("Period", dt::int32_t);

        auto& collectable_tns_tbl = schema.addTable("CollectableTreeNodes");
        collectable_tns_tbl.addColumn("SerializationCID", dt::uint32_t);
        collectable_tns_tbl.addColumn("FullPath", dt::string_t);
        collectable_tns_tbl.addColumn("ClockID", dt::int32_t);
        collectable_tns_tbl.addColumn("TypeName", dt::string_t);
        collectable_tns_tbl.ensureUnique("SerializationCID");
        collectable_tns_tbl.unsetPrimaryKey();

        auto& collection_records_tbl = schema.addTable("CollectionRecords");
        collection_records_tbl.addColumn("TimestampID", dt::int32_t);
        collection_records_tbl.addColumn("Records", dt::blob_t); // compressed
        collection_records_tbl.unsetPrimaryKey();

        auto& queue_max_sizes_tbl = schema.addTable("QueueMaxSizes");
        queue_max_sizes_tbl.addColumn("CollectableTreeNodeID", dt::int32_t);
        queue_max_sizes_tbl.addColumn("MaxSize", dt::int32_t);
    }

    /// \brief Return the string intern table used when mapping string values to stable integer IDs for storage.
    simdb::TinyStrings<>* getTinyStrings() const
    {
        return collection_->getTinyStrings();
    }

    /// \brief Create the pipeline to process collected data.
    void createPipeline(pipeline::PipelineManager* pipeline_mgr) override
    {
        auto pipeline = pipeline_mgr->createPipeline(NAME, this);

        pipeline->addStage<Compressor>("compressor");
        pipeline->addStage<Writer>("writer");
        pipeline->noMoreStages();

        pipeline->bind("compressor.output_queue", "writer.input_queue");
        pipeline->noMoreBindings();

        auto pipeline_head = pipeline->getInPortQueue<QueueCollectionData>("compressor.input_queue");
        collection_->connectToPipeline(pipeline_head);
    }

    /// \brief Run after initialization; invokes \ref CollectionPipelineMeta::writeMetaOnPostInit.
    void postInit(int, char**) override
    {
        collection_->writeMetaOnPostInit(db_mgr_);
    }

    /// \brief Perform any last minute flushes to the pipeline before threads are shutdown
    /// and the pipeline is flushed.
    void preTeardown() override
    {
        collection_->sendCollectedDataToPipeline();
    }

    /// \brief Perform end-of-simulation tasks
    void postTeardown() override
    {
        if (auto tiny_strings = getTinyStrings())
        {
            tiny_strings->serialize();
        }
        collection_->writeMetaOnPostTeardown(db_mgr_);
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

    class Writer : public pipeline::DatabaseStage<CollectionPipeline>
    {
    public:
        Writer()
        {
            addInPort_<CompressedQueueCollectionData>("input_queue", input_queue_);
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

            return pipeline::PipelineAction::SLEEP;
        }

        ConcurrentQueue<CompressedQueueCollectionData>* input_queue_ = nullptr;
    };

    DatabaseManager *const db_mgr_;
    CollectionBase* collection_ = nullptr;
};

} // namespace simdb::collection
