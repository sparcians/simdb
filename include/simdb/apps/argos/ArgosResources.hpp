// <ArgosResources.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace simdb::argos {

//! This class allows us to track collected enum values (int) and their corresponding
//! stringified values (operator<<) and store them in the database for the Argos UI
//! to use. Note that enums without operator<< are just shown in Argos as their int
//! values.
class EnumInspector
{
public:
    template <typename E> std::enable_if_t<type_traits::has_ostream_operator_v<E>, void> inspect(E val)
    {
        static EnumMap<E>* map = [this] {
            auto& slot = enum_maps_[simdb::demangle_type<E>()];
            if (!slot)
            {
                slot = std::make_unique<EnumMap<E>>();
            }
            return static_cast<EnumMap<E>*>(slot.get());
        }();
        map->inspect(val);
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
            if (last_seen_ && *last_seen_ == val)
            {
                return;
            }

            for (E seen : all_seen_)
            {
                if (seen == val)
                {
                    last_seen_ = val;
                    return;
                }
            }

            all_seen_.push_back(val);
            last_seen_ = val;
        }

        void dumpEnumMap(DatabaseManager* db_mgr) const override final
        {
            using underlying_t = std::underlying_type_t<E>;
            using dump_int_t = std::conditional_t<std::is_signed_v<underlying_t>, int64_t, uint64_t>;
            using enum_map_t = std::map<dump_int_t, std::string>;

            enum_map_t enum_map;
            for (auto e : all_seen_)
            {
                std::ostringstream oss;
                oss << e;
                enum_map[static_cast<dump_int_t>(e)] = oss.str();
            }

            dumpEnumMap_(db_mgr, enum_map);
        }

    private:
        template <typename IntType>
        void dumpEnumMap_(DatabaseManager* db_mgr, const std::map<IntType, std::string>& enum_map) const
        {
            constexpr auto table_name = std::is_signed_v<IntType> ? "SignedEnumMappings" : "UnsignedEnumMappings";
            auto inserter = db_mgr->prepareINSERT(SQL_TABLE(table_name));

            const auto enum_name = demangle_type<E>();
            for (const auto& [enum_val, enum_str] : enum_map)
            {
                inserter->createRecordWithColValues(enum_name, enum_str, enum_val);
            }
        }

        std::vector<E> all_seen_;
        std::optional<E> last_seen_;
    };

    std::unordered_map<std::string, std::unique_ptr<EnumMapBase>> enum_maps_;
};

class ArgosResources
{
public:
    explicit ArgosResources(TinyStrings<>* tiny_strings) :
        tiny_strings_(tiny_strings)
    {
    }

    TinyStrings<>* getTinyStrings() const { return tiny_strings_; }

    EnumInspector* getEnumInspector() { return &enum_map_resource_; }

    void writeMetaOnPostTeardown(DatabaseManager* db_mgr)
    {
        tiny_strings_->serialize(db_mgr);
        enum_map_resource_.serializeEnumMaps(db_mgr);
    }

private:
    TinyStrings<>* tiny_strings_;
    EnumInspector enum_map_resource_;
};

} // namespace simdb::argos
