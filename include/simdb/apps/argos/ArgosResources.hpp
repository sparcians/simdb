// <ArgosResources.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/pipeline/Pipeline.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include <filesystem>
#include <random>

namespace simdb::argos {

class ArgosResources;

class HeartbeatResource
{
public:
    explicit HeartbeatResource(ArgosResources* resource_container);
    virtual void setHeartbeat(size_t heartbeat) = 0;
};

class PipelineResource
{
public:
    explicit PipelineResource(ArgosResources* resource_container);
    virtual void setPipeline(pipeline::Pipeline* pipeline) = 0;
};

class TimestampResource
{
public:
    explicit TimestampResource(ArgosResources* resource_container);
    virtual void setTimestamp(Timestamp* timestamp) = 0;
};

class DatabaseResource
{
public:
    explicit DatabaseResource(ArgosResources* resource_container);
    virtual void setDatabase(DatabaseManager* db_mgr) = 0;
};

class PipelineStagerResource : public HeartbeatResource, public PipelineResource, public TimestampResource
{
public:
    explicit PipelineStagerResource(ArgosResources* resource_container) :
        HeartbeatResource(resource_container),
        PipelineResource(resource_container),
        TimestampResource(resource_container)
    {
    }

    void setHeartbeat(size_t heartbeat) override final
    {
        checkNotReady_();
        heartbeat_ = heartbeat;
        checkReady_();
    }

    void setPipeline(pipeline::Pipeline* pipeline) override final
    {
        checkNotReady_();
        pipeline_ = pipeline;
        checkReady_();
    }

    void setTimestamp(Timestamp* timestamp) override final
    {
        checkNotReady_();
        timestamp_ = timestamp;
        checkReady_();
    }

    PipelineStager* operator->() const { return get(); }

    operator PipelineStager*() const { return get(); }

    PipelineStager* get() const { return stager_.get(); }

private:
    void checkNotReady_()
    {
        if (realized_)
        {
            throw DBException("PipelineStager resource already created!");
        }
    }

    void checkReady_()
    {
        if (!realized_ && heartbeat_.isValid() && pipeline_ && timestamp_)
        {
            auto pipeline_head = pipeline_->getInPortQueue<QueueCollectionData>("compressor.input_queue");
            auto notif_head = pipeline_->getInPortQueue<Notification>("writer.notif_queue");
            auto dyn_field_head = pipeline_->getInPortQueue<DynamicFieldChanges>("writer.dyn_field_queue");
            stager_ =
                std::make_unique<PipelineStager>(heartbeat_, timestamp_, pipeline_head, notif_head, dyn_field_head);

            // Flush pending notifications (does not apply to other ConcurrentQueue's)
            Notification notif;
            while (dummy_notif_head_.try_pop(notif))
            {
                stager_->postNotif(notif.cid, notif.notif, notif.type);
            }

            realized_ = true;
        }
    }

    ValidValue<size_t> heartbeat_;
    pipeline::Pipeline* pipeline_ = nullptr;
    Timestamp* timestamp_ = nullptr;

    ConcurrentQueue<Notification> dummy_notif_head_;
    std::unique_ptr<PipelineStager> stager_{new PipelineStager(0, nullptr, nullptr, &dummy_notif_head_, nullptr)};
    bool realized_ = false;
};

class TinyStringsResource : public DatabaseResource
{
public:
    explicit TinyStringsResource(ArgosResources* resource_container) :
        DatabaseResource(resource_container)
    {
    }

    void setDatabase(DatabaseManager* db_mgr) override final
    {
        if (realized_)
        {
            throw DBException("TinyStrings resource already created!");
        }

        tiny_strings_ = std::make_unique<TinyStrings<>>(db_mgr);
        if (tiny_strings_->serializedCount())
        {
            throw DBException("TinyStrings resource cannot accept a DB that already has TinyStringIDs!");
        }

        auto query = tmp_db_.createQuery("TinyStringIDs");

        std::string string_val;
        query->select("StringValue", string_val);

        std::set<std::string> new_strings;
        auto results = query->getResultSet();
        while (results.getNextRecord())
        {
            new_strings.insert(string_val);
        }

        tiny_strings_->batchInsert(new_strings);
        realized_ = true;
    }

    TinyStrings<>* operator->() const { return get(); }

    operator TinyStrings<>*() const { return get(); }

    TinyStrings<>* get() const { return tiny_strings_.get(); }

private:
    static std::filesystem::path makeTempFile_()
    {
        const auto temp_dir = std::filesystem::temp_directory_path();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        return temp_dir / (std::to_string(dist(gen)) + ".db");
    }

    DatabaseManager tmp_db_{makeTempFile_(), true};
    std::unique_ptr<TinyStrings<>> tiny_strings_{new TinyStrings<>(&tmp_db_)};
    bool realized_ = false;
};

class CollectedDataResource
{
public:
    explicit CollectedDataResource(TinyStringsResource& tiny_strings) :
        tiny_strings_(tiny_strings)
    {
    }

    CollectedData& getFor(uint16_t cid)
    {
        auto& data = collected_data_map_[cid];
        if (!data)
        {
            data = std::make_unique<CollectedData>(cid, tiny_strings_.get());
        }
        return *data;
    }

private:
    TinyStringsResource& tiny_strings_;
    std::unordered_map<uint16_t, std::unique_ptr<CollectedData>> collected_data_map_;
};

class ArgosResources
{
public:
    void addResource(HeartbeatResource* resource) { heartbeat_resources_.push_back(resource); }
    void addResource(PipelineResource* resource) { pipeline_resources_.push_back(resource); }
    void addResource(TimestampResource* resource) { timestamp_resources_.push_back(resource); }
    void addResource(DatabaseResource* resource) { database_resources_.push_back(resource); }

    void setHeartbeat(size_t heartbeat)
    {
        for (auto r : heartbeat_resources_)
        {
            r->setHeartbeat(heartbeat);
        }
    }

    void setPipeline(pipeline::Pipeline* pipeline)
    {
        for (auto r : pipeline_resources_)
        {
            r->setPipeline(pipeline);
        }
    }

    void setTimestamp(Timestamp* timestamp)
    {
        for (auto r : timestamp_resources_)
        {
            r->setTimestamp(timestamp);
        }
    }

    void setDatabase(DatabaseManager* db_mgr)
    {
        for (auto r : database_resources_)
        {
            r->setDatabase(db_mgr);
        }
    }

    PipelineStagerResource& getStager() { return stager_resource_; }

    TinyStringsResource& getTinyStrings() { return tiny_strings_resource_; }

    CollectedDataResource& getCollectedDataBuffers() { return collected_data_bufs_resource_; }

private:
    std::vector<HeartbeatResource*> heartbeat_resources_;
    std::vector<PipelineResource*> pipeline_resources_;
    std::vector<TimestampResource*> timestamp_resources_;
    std::vector<DatabaseResource*> database_resources_;

    PipelineStagerResource stager_resource_{this};
    TinyStringsResource tiny_strings_resource_{this};
    CollectedDataResource collected_data_bufs_resource_{tiny_strings_resource_};
};

inline HeartbeatResource::HeartbeatResource(ArgosResources* resource_container)
{
    resource_container->addResource(this);
}

inline PipelineResource::PipelineResource(ArgosResources* resource_container)
{
    resource_container->addResource(this);
}

inline TimestampResource::TimestampResource(ArgosResources* resource_container)
{
    resource_container->addResource(this);
}

inline DatabaseResource::DatabaseResource(ArgosResources* resource_container)
{
    resource_container->addResource(this);
}

} // namespace simdb::argos
