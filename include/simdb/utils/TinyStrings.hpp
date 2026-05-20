// <TinyStrings.hpp> -*- C++ -*-

#pragma once

#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/sqlite/PreparedINSERT.hpp"
#include "simdb/utils/DeferredLock.hpp"
#include "simdb/utils/TypeTraits.hpp"
#include <mutex>

namespace simdb {

/// To keep SimDB collection as fast and small as possible, we serialize strings
/// not as actual strings, but as ints. This class is used to map strings to
/// ints, and is periodically serialized to a database table.
template <bool MutexProtect = false> class TinyStrings
{
public:
    static inline constexpr uint32_t BAD_STRING_ID = 0;

    /// Associate this TinyStrings with the given database.
    TinyStrings(DatabaseManager* db_mgr) :
        db_mgr_(db_mgr)
    {
        const auto& schema = db_mgr_->getSchema();
        if (!schema.hasTable("TinyStringIDs"))
        {
            Schema append_schema;
            using dt = simdb::SqlDataType;
            auto& tbl = append_schema.addTable("TinyStringIDs");
            tbl.addColumn("StringValue", dt::string_t);
            tbl.addColumn("StringID", dt::uint32_t);
            db_mgr->appendSchema(append_schema);
        } else
        {
            auto query = db_mgr->createQuery("TinyStringIDs");

            std::string str;
            query->select("StringValue", str);

            uint32_t id;
            query->select("StringID", id);

            auto results = query->getResultSet();
            while (results.getNextRecord())
            {
                assert(map_->find(str) == map_->end());
                (*map_)[str] = id;
            }
        }
    }

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

    /// Get a string ID for the given string.
    uint32_t getStringID(const std::unique_ptr<std::string>& s) { return getStringID(s.get()); }

    /// Get a string ID for the given string.
    uint32_t getStringID(const std::shared_ptr<std::string>& s) { return getStringID(s.get()); }

    /// Get a string ID for the given string.
    uint32_t getStringID(const std::string* s)
    {
        if (s)
        {
            return getStringID(*s);
        }
        return BAD_STRING_ID;
    }

    /// std::string overload
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

    /// Serialize the current string map to the database.
    void serialize()
    {
        db_mgr_->safeTransaction([&]() {
            DeferredLock<std::mutex> lock(mutex_);
            if constexpr (MutexProtect)
            {
                lock.lock();
            }

            auto inserter = db_mgr_->prepareINSERT(SQL_TABLE("TinyStringIDs"));
            for (const auto& [string_id, string_val] : unserialized_map_)
            {
                inserter->createRecordWithColValues(string_val, string_id);
            }

            unserialized_map_.clear();
        });
    }

    /// Check how many unserialized strings we have.
    uint32_t getUnserializedCount() const noexcept
    {
        DeferredLock<std::mutex> lock(mutex_);
        if constexpr (MutexProtect)
        {
            lock.lock();
        }
        return unserialized_map_.size();
    }

    /// Get a string for a given ID. Not intended to be
    /// called in performance critical code.
    std::string getString(const uint32_t string_id, bool must_exist = true) const
    {
        for (const auto& [s, id] : *map_)
        {
            if (id == string_id)
            {
                return s;
            }
        }

        if (must_exist)
        {
            throw simdb::DBException("String ID not in TinyStrings: ") << string_id;
        }
        return "";
    }

private:
    uint32_t getStringID_(const std::string& s)
    {
        auto iter = map_->find(s);
        if (iter == map_->end())
        {
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

    DatabaseManager* const db_mgr_;
    mutable std::mutex mutex_;
};

} // namespace simdb
