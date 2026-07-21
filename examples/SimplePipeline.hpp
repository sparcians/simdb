// <SimplePipeline.hpp> -*- C++ -*-

#pragma once

#include "SimDBTester.hpp"
#include "simdb/apps/App.hpp"
#include "simdb/pipeline/Pipeline.hpp"
#include "simdb/pipeline/PipelineManager.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/Compress.hpp"

/// This class demonstrates a simple pipeline design using SimDB.
/// It is reused by most of the examples.

class SimplePipeline : public simdb::App
{
public:
    static constexpr auto NAME = "simple-pipeline";

    SimplePipeline(simdb::DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
    }

    ~SimplePipeline() noexcept = default;

    static void defineSchema(simdb::Schema& schema)
    {
        using dt = simdb::SqlDataType;

        auto& tbl = schema.addTable("CompressedData");
        tbl.addColumn("AppInstance", dt::int32_t);
        tbl.addColumn("DataBlob", dt::blob_t);
    }

    void createPipeline(simdb::pipeline::PipelineManager* pipeline_mgr) override
    {
        auto pipeline = pipeline_mgr->createPipeline(NAME, this);

        pipeline->addStage<CompressionStage>("compressor");
        pipeline->addStage<DatabaseStage>("db_writer", getInstance());
        pipeline->noMoreStages();

        pipeline->bind("compressor.compressed_data", "db_writer.data_to_write");
        pipeline->noMoreBindings();

        // As soon as we call noMoreBindings(), all input/output queues are
        // available
        pipeline_head_ = pipeline->getInPortQueue<std::vector<double>>("compressor.input_data");

        // Store a pipeline flusher (flush compressor first, then DB writer)
        pipeline_flusher_ = pipeline->createFlusher({"compressor", "db_writer"});

        // Store the pipeline manager so we can test ScopedRunnableDisabler as
        // we as access the AsyncDatabaseAccessor later.
        pipeline_mgr_ = pipeline_mgr;
    }

    void process(const std::vector<double>& data) { pipeline_head_->push(data); }

protected:
    simdb::DatabaseManager* db_mgr_ = nullptr;
    simdb::ConcurrentQueue<std::vector<double>>* pipeline_head_ = nullptr;
    simdb::pipeline::PipelineManager* pipeline_mgr_ = nullptr;
    std::unique_ptr<simdb::pipeline::Flusher> pipeline_flusher_;

private:
    /// Compress on pipeline thread 1
    class CompressionStage : public simdb::pipeline::CompressionStage
    {
    public:
        CompressionStage()
        {
            addInPort_<std::vector<double>>("input_data", input_queue_);
            addOutPort_<std::vector<char>>("compressed_data", output_queue_);

            // Ensure that the AsyncDatabaseAccessor is null - no threads have
            // been created yet
            EXPECT_EQUAL(getAsyncDatabaseAccessor_(), nullptr);
        }

    private:
        simdb::pipeline::PipelineAction run_(bool) override
        {
            std::vector<double> data;
            if (input_queue_->try_pop(data))
            {
                std::vector<char> compressed_data;
                simdb::compressData(data, compressed_data, getBestCompressionLevel_());
                output_queue_->emplace(std::move(compressed_data));
                return simdb::pipeline::PROCEED;
            }

            return simdb::pipeline::SLEEP;
        }

        simdb::ConcurrentQueue<std::vector<double>>* input_queue_ = nullptr;
        simdb::ConcurrentQueue<std::vector<char>>* output_queue_ = nullptr;
    };

    /// Write to SQLite on dedicated database thread
    class DatabaseStage : public simdb::pipeline::DatabaseStage<SimplePipeline>
    {
    public:
        DatabaseStage(size_t app_instance_num) :
            app_instance_num_(app_instance_num)
        {
            addInPort_<std::vector<char>>("data_to_write", input_queue_);
        }

    private:
        simdb::pipeline::PipelineAction run_(bool) override
        {
            // Ensure we cannot get the AsyncDatabaseAccessor - we are already
            // going to be on the database thread, and should just use the
            // DatabaseManager directly (getDatabaseManager_).
            EXPECT_THROW(getAsyncDatabaseAccessor_());

            std::vector<char> data;
            if (input_queue_->try_pop(data))
            {
                auto inserter = getTableInserter_("CompressedData");
                inserter->setColumnValue(0, (int)app_instance_num_);
                inserter->setColumnValue(1, data);
                inserter->createRecord();
                return simdb::pipeline::PROCEED;
            }

            return simdb::pipeline::SLEEP;
        }

        size_t app_instance_num_ = 0;
        simdb::ConcurrentQueue<std::vector<char>>* input_queue_ = nullptr;
    };
};
