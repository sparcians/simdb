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

//! There are collection classes that require things like DatabaseManager,
//! collection heartbeat, etc. in order to be instantiated. We cannot know
//! ahead of time all the ways in which these ctor args will become available
//! for all simulations / unit tests. One simulation could do this:
//!
//!   - set the heartbeat value
//!   - set the timestamp backpointer
//!   - create the DatabaseManager         (now TinyStrings can be created)
//!   - open pipelines                     (now PipelineStager can be created)
//!
//! But a unit test might do this:
//!
//!   - set the timestamp backpointer
//!   - set the heartbeat value
//!   - never create the DatabaseManager
//!   - never open pipelines
//!
//! In the unit test, we would never have a DatabaseManager, so we would
//! never have a TinyStrings (whose ctor takes a DatabaseManager).
//!
//! The classes below provide a way for Argos collection to freely use temporary
//! resources like TinyStrings and PipelineStager until all required ctor args
//! have been set, then the "live" resources like TinyStrings are created and
//! pre-populated with any information gathered while the temporary objects
//! were being used.

namespace simdb::argos {

class ArgosResources;

//! For lazy creation of resources that require the pipeline heartbeat
class HeartbeatResource
{
public:
    explicit HeartbeatResource(ArgosResources* resource_container);
    virtual void setHeartbeat(size_t heartbeat) = 0;
};

//! For lazy creation of resources that require the Pipeline
class PipelineResource
{
public:
    explicit PipelineResource(ArgosResources* resource_container);
    virtual void setPipeline(pipeline::Pipeline* pipeline) = 0;
};

//! For lazy creation of resources that require the Timestamp
class TimestampResource
{
public:
    explicit TimestampResource(ArgosResources* resource_container);
    virtual void setTimestamp(Timestamp* timestamp) = 0;
};

//! For lazy creation of resources that require the DatabaseManager
class DatabaseResource
{
public:
    explicit DatabaseResource(ArgosResources* resource_container);
    virtual void setDatabase(DatabaseManager* db_mgr) = 0;
};

//! \class PipelineStagerResource
//! \brief Used to lazily create a "live" PipelineStager only when the
//! heartbeat is known, the pipeline has been created, and the timestamp
//! has been created. Those three bits of information can be set in any
//! order. If one or more is never set, as in the case of some unit tests,
//! then the ArgosCollector will be using a temporary PipelineStager.
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

    //! Access the temporary/live PipelineStager. DO NOT cache this raw
    //! pointer. If you accidentally cache the temporary stager, you will
    //! see a crash if it gets reallocated to the live stager.
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

            // Flush pending notifications. There are use cases where we might
            // need to send warnings/errors to Argos before e.g. the pipeline
            // was opened or before the heartbeat was known.
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

    //! Temporary queue for Notifications received while using the temp PipelineStager
    ConcurrentQueue<Notification> dummy_notif_head_;

    //! PipelineStager - starts out as the temporary stager, reallocated when
    //! we get everything we need for the live stager
    std::shared_ptr<PipelineStager> stager_{
        std::make_shared<PipelineStager>(0, nullptr, nullptr, &dummy_notif_head_, nullptr)};
    bool realized_ = false;
};

//! This class is used to manage the TinyStrings resource before/after the
//! DatabaseManager is first seen.
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
        } else if (realized_)
        {
            return;
        }

        auto copy_from = tiny_strings_.get();
        auto new_tiny_strings = std::make_shared<TinyStrings<>>(db_mgr, copy_from);
        tiny_strings_ = std::move(new_tiny_strings);
        realized_ = true;
    }

    //! Access the temporary/live TinyStrings. DO NOT cache this raw
    //! pointer. If you accidentally cache the temporary TinyStrings,
    //! you will see a crash if it gets reallocated to the live one.
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

//! This class manages CollectedData resources before/after the live TinyStrings is created.
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

//! This class allows us to track collected enum values (int) and their corresponding
//! stringified values (operator<<) and store them in the database for the Argos UI
//! to use. Note that enums without operator<< are just shown in Argos as their int
//! values.
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
            using underlying_t = std::underlying_type_t<E>;
            using DumpInt = std::conditional_t<std::is_signed_v<underlying_t>, int64_t, uint64_t>;
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
    template <typename E> std::enable_if_t<type_traits::has_ostream_operator_v<E>, void> inspect(E val)
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

    template <typename E> std::enable_if_t<!type_traits::has_ostream_operator_v<E>, void> inspect(E) {}

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
        tiny_strings_resource_->serialize();
        enum_map_resource_.serializeEnumMaps(db_mgr);
        stager_resource_->writeMetaOnPostTeardown(db_mgr);
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
