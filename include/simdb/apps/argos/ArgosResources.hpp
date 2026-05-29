// <ArgosResources.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/apps/argos/PipelineStager.hpp"
#include "simdb/pipeline/Pipeline.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/SafeWeakPtr.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include <filesystem>
#include <memory>
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

    PipelineStager* operator->() const { return get().operator->(); }

    safe_weak_ptr<PipelineStager> get() const { return stager_; }

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
                std::make_shared<PipelineStager>(heartbeat_, timestamp_, pipeline_head, notif_head, dyn_field_head);

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
    std::shared_ptr<PipelineStager> stager_{
        std::make_shared<PipelineStager>(0, nullptr, nullptr, &dummy_notif_head_, nullptr)};
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
        if (realized_ && tiny_strings_->getDatabaseManager() != db_mgr)
        {
            throw DBException("TinyStrings resource already created!");
        }

        auto copy_from = tiny_strings_.get();
        auto new_tiny_strings = std::make_shared<TinyStrings<>>(db_mgr, copy_from);
        tiny_strings_ = std::move(new_tiny_strings);
        realized_ = true;
    }

    TinyStrings<>* operator->() const { return get().operator->(); }

    safe_weak_ptr<TinyStrings<>> get() const { return tiny_strings_; }

    void serialize() { tiny_strings_->serialize(); }

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
    std::shared_ptr<TinyStrings<>> tiny_strings_{std::make_shared<TinyStrings<>>(&tmp_db_)};
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
        const auto live = tiny_strings_.get();
        if (!data || data->usesExpiredTinyStrings(live))
        {
            data = std::make_unique<CollectedData>(cid, live);
        }
        return *data;
    }

private:
    TinyStringsResource& tiny_strings_;
    std::unordered_map<uint16_t, std::unique_ptr<CollectedData>> collected_data_map_;
};

class EnumMapResource
{
    class EnumMapBase
    {
    public:
        virtual ~EnumMapBase() = default;
        virtual void dumpEnumMap(simdb::DatabaseManager* db_mgr) const = 0;
    };

    template <typename E, typename = void> class EnumMap : public EnumMapBase
    {
    public:
        void inspect(E) {}
        void dumpEnumMap(simdb::DatabaseManager*) const override final {}
    };

    template <typename E>
    class EnumMap<E, std::enable_if_t<type_traits::has_ostream_operator_v<E>>> : public EnumMapBase
    {
    public:
        void inspect(E val)
        {
            auto& s = enum_map_[val];
            if (s.empty())
            {
                std::ostringstream oss;
                oss << val;
                s = oss.str();
                sparta_assert(!s.empty());
            }
        }

        void dumpEnumMap(DatabaseManager* db_mgr) const override final
        {
            using Underlying = std::underlying_type_t<E>;
            using DumpInt = std::conditional_t<std::is_signed_v<Underlying>, int64_t, uint64_t>;
            dumpEnumMap_<DumpInt>(db_mgr);
        }

    private:
        template <typename IntType> void dumpEnumMap_(DatabaseManager* db_mgr) const
        {
            constexpr auto table_name = std::is_signed_v<IntType> ? "SignedEnumMappings" : "UnsignedEnumMappings";
            auto inserter = db_mgr->prepareINSERT(SQL_TABLE(table_name));

            const auto enum_name = demangle_type<E>();
            for (const auto& [enum_val, enum_str] : enum_map_)
            {
                inserter->createRecordWithColValues(enum_name, enum_str, static_cast<IntType>(enum_val));
            }
        }

        std::map<E, std::string> enum_map_;
    };

public:
    template <typename E> void inspect(E val)
    {
        auto& enum_map = enum_maps_[simdb::demangle_type<E>()];
        if (!enum_map)
        {
            enum_map = std::make_unique<EnumMap<E>>();
        }
        EnumMapBase* abstract_map = enum_map.get();
        auto concrete_map = static_cast<EnumMap<E>*>(abstract_map);
        concrete_map->inspect(val);
    }

    void serializeEnumMaps(DatabaseManager* db_mgr)
    {
        for (auto& [_, map] : enum_maps_)
        {
            map->dumpEnumMap(db_mgr);
        }
    }

private:
    std::unordered_map<std::string, std::unique_ptr<EnumMapBase>> enum_maps_;
};

class ArgosResources
{
public:
    template <typename Resource> void addResource(Resource* resource)
    {
        if constexpr (std::is_base_of<HeartbeatResource, Resource>::value)
        {
            heartbeat_resources_.push_back(resource);
        }
        if constexpr (std::is_base_of<PipelineResource, Resource>::value)
        {
            pipeline_resources_.push_back(resource);
        }
        if constexpr (std::is_base_of<TimestampResource, Resource>::value)
        {
            timestamp_resources_.push_back(resource);
        }
        if constexpr (std::is_base_of<DatabaseResource, Resource>::value)
        {
            database_resources_.push_back(resource);
        }
    }

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

    PipelineStagerResource& getStagerResource() { return stager_resource_; }

    TinyStringsResource& getTinyStringsResource() { return tiny_strings_resource_; }

    CollectedDataResource& getCollectedDataBuffersResource() { return collected_data_bufs_resource_; }

    EnumMapResource* getEnumMapResource() { return &enum_map_resource_; }

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr)
    {
        tiny_strings_resource_.serialize();
        enum_map_resource_.serializeEnumMaps(db_mgr);
    }

private:
    std::vector<HeartbeatResource*> heartbeat_resources_;
    std::vector<PipelineResource*> pipeline_resources_;
    std::vector<TimestampResource*> timestamp_resources_;
    std::vector<DatabaseResource*> database_resources_;

    PipelineStagerResource stager_resource_{this};
    TinyStringsResource tiny_strings_resource_{this};
    CollectedDataResource collected_data_bufs_resource_{tiny_strings_resource_};
    EnumMapResource enum_map_resource_;
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
