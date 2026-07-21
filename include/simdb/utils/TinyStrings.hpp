// <TinyStrings.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/Exceptions.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/sqlite/PreparedINSERT.hpp"
#include "simdb/utils/DeferredLock.hpp"
#include "simdb/utils/TypeTraits.hpp"
#include <mutex>

namespace simdb {

/// To keep SimDB collection as fast and small as possible, we serialize strings
/// not as actual strings, but as ints. This class is used to map strings to
/// ints in memory, and may be serialized to a database table at teardown.
template <bool MutexProtect = false> class TinyStrings
{
public:
    static inline constexpr uint32_t BAD_STRING_ID = 0;

    TinyStrings() = default;

    /// Add or get a string ID for the given string.
    std::pair<uint32_t, bool> insert(const std::string& s)
    {
        DeferredLock<std::mutex> lock(mutex_);
        if constexpr (MutexProtect)
        {
            lock.lock();
        }

        auto iter = map_->find(s);
        if (iter == map_->end())
        {
            return std::make_pair(getStringID_(s), true);
        } else
        {
            return std::make_pair(getStringID_(s), false);
        }
    }

    /// Get a string ID for the given string
    uint32_t getStringID(const std::string& s)
    {
        if (s.empty())
        {
            return BAD_STRING_ID;
        }

        DeferredLock<std::mutex> lock(mutex_);
        if constexpr (MutexProtect)
        {
            lock.lock();
        }

        return getStringID_(s);
    }

    /// Get a string ID for the given string
    uint32_t getStringID(const char* s)
    {
        assert(s != nullptr);
        return getStringID(std::string(s));
    }

    /// Get a string ID for the given string (pointer)
    template <typename S>
    type_traits::enable_if_t<!type_traits::is_char_pointer_v<S> && type_traits::is_any_pointer_v<S>, uint32_t>
    getStringID(const S& s)
    {
        if (s)
        {
            return getStringID(*s);
        }
        return BAD_STRING_ID;
    }

    /// Serialize newly seen string mappings to the database.
    void serialize(DatabaseManager* db_mgr)
    {
        simdb_assert(db_mgr, "TinyStrings::serialize requires a DatabaseManager");

        db_mgr->safeTransaction([&]() {
            DeferredLock<std::mutex> lock(mutex_);
            if constexpr (MutexProtect)
            {
                lock.lock();
            }

            if (allowed_db_ == nullptr)
            {
                allowed_db_ = db_mgr;
            } else
            {
                simdb_assert(allowed_db_ == db_mgr, "TinyStrings::serialize may only target one DatabaseManager");
            }

            const auto& schema = db_mgr->getSchema();
            if (!schema.hasTable("TinyStringIDs"))
            {
                Schema append_schema;
                using dt = simdb::SqlDataType;
                auto& tbl = append_schema.addTable("TinyStringIDs");
                tbl.addColumn("StringValue", dt::string_t);
                tbl.addColumn("StringID", dt::uint32_t);
                db_mgr->appendSchema(append_schema);
            }

            auto inserter = db_mgr->prepareINSERT(SQL_TABLE("TinyStringIDs"));
            for (const auto& [string_id, string_val] : unserialized_map_)
            {
                inserter->createRecordWithColValues(string_val, string_id);
            }

            unserialized_map_.clear();
        });
    }

private:
    uint32_t getStringID_(const std::string& s)
    {
        auto iter = map_->find(s);
        if (iter == map_->end())
        {
            simdb_assert(map_->size() != UINT32_MAX, "Too many TinyStrings created. UINT32_MAX has been reached.");

            uint32_t id = map_->size() + 1;
            map_->insert({s, id});
            unserialized_map_.insert({id, s});
            return id;
        } else
        {
            return iter->second;
        }
    }

    using string_map_t = std::shared_ptr<std::unordered_map<std::string, uint32_t>>;
    using unserialized_string_map_t = std::unordered_map<uint32_t, std::string>;

    string_map_t map_ = std::make_shared<std::unordered_map<std::string, uint32_t>>();
    unserialized_string_map_t unserialized_map_;

    DatabaseManager* allowed_db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace simdb
