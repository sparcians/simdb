// <Timestamps.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"

#include <functional>

namespace simdb::argos {

/// \class Timestamp
/// \brief Timestamp that can get current time values via a backpointer,
/// C-style function, or a std::function
class Timestamp
{
public:
    /// \brief Construct with a backpointer to get the current time value
    Timestamp(const uint64_t* backpointer) :
        backpointer_(backpointer)
    {
        assert(backpointer);
    }

    /// \brief Construct with a C-style function pointer to get the current time value
    Timestamp(uint64_t (*fn)()) :
        cfuncpointer_(fn)
    {
        assert(fn);
    }

    /// \brief Construct with a std::function to get the current time value
    Timestamp(std::function<uint64_t()> fn) :
        stdfunction_(fn)
    {
        assert(fn);
    }

    /// Read the current simulation time
    uint64_t getTime() const
    {
        if (backpointer_)
        {
            return *backpointer_;
        }
        if (cfuncpointer_)
        {
            return cfuncpointer_();
        }
        return stdfunction_();
    }

    /// Create or look up a Timestamps table row for the given simulation time; return rowid.
    static int createTimestampInDatabase(DatabaseManager* db_mgr, uint64_t time)
    {
        // Ensure we don't create multiple entries in the Timestamps table
        // that have the same Timestamp column value (it will throw; must
        // be unique).
        //
        // Note that this is called on the DB thread and it is not a big
        // performance issue to query alongside the INSERT.
        auto query = db_mgr->createQuery("Timestamps");
        query->addConstraintForUInt64("Timestamp", Constraints::EQUAL, time);

        int id;
        query->select("Id", id);

        if (query->getResultSet().getNextRecord())
        {
            assert(id > 0);
            return id;
        }

        return db_mgr->INSERT(SQL_TABLE("Timestamps"), SQL_VALUES(time))->getId();
    }

private:
    const uint64_t* backpointer_ = nullptr;
    uint64_t (*cfuncpointer_)() = nullptr;
    std::function<uint64_t()> stdfunction_ = nullptr;
};

} // namespace simdb::argos
