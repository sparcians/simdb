// <EnumInspector.hpp> -*- C++ -*-

#pragma once

#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/TypeTraits.hpp"

#include <memory>
#include <optional>
#include <unordered_map>
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
            const auto itype = demangle_type<underlying_t>();
            const auto enum_name = demangle_type<E>();
            const auto enum_id = db_mgr->INSERT(SQL_TABLE("CollectedEnums"), SQL_VALUES(enum_name, itype))->getId();

            auto inserter = db_mgr->prepareINSERT(SQL_TABLE("EnumMembers"));
            for (auto e : all_seen_)
            {
                std::ostringstream oss;
                oss << e;

                const auto member_name = oss.str();
                const auto raw_enum_val = static_cast<underlying_t>(e);
                const auto raw_enum_str = std::to_string(raw_enum_val);

                inserter->createRecordWithColValues(enum_id, member_name, raw_enum_str);
            }
        }

    private:
        std::vector<E> all_seen_;
        std::optional<E> last_seen_;
    };

    std::unordered_map<std::string, std::unique_ptr<EnumMapBase>> enum_maps_;
};

} // namespace simdb::argos
