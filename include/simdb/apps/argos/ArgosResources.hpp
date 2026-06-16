// <ArgosResources.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/SafeWeakPtr.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include <filesystem>
#include <memory>
#include <random>
#include <unordered_set>

namespace simdb::argos {

class ArgosResources;

//! For lazy creation of resources that require the DatabaseManager
class DatabaseResource
{
public:
    explicit DatabaseResource(ArgosResources* resource_container);
    virtual void setDatabase(DatabaseManager* db_mgr) = 0;
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
        if constexpr (std::is_base_of<DatabaseResource, Resource>::value)
        {
            database_resources_.push_back(resource);
        }
    }

    void setDatabase(DatabaseManager* db_mgr)
    {
        for (auto r : database_resources_)
        {
            r->setDatabase(db_mgr);
        }
    }

    TinyStringsResource& getTinyStringsResource() { return tiny_strings_resource_; }

    EnumMapResource* getEnumMapResource() { return &enum_map_resource_; }

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr)
    {
        tiny_strings_resource_->serialize();
        enum_map_resource_.serializeEnumMaps(db_mgr);
    }

private:
    std::vector<DatabaseResource*> database_resources_;
    TinyStringsResource tiny_strings_resource_{this};
    EnumMapResource enum_map_resource_;
};

inline DatabaseResource::DatabaseResource(ArgosResources* resource_container)
{
    resource_container->addResource(this);
}

} // namespace simdb::argos
