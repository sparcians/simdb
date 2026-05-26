// <Timestamps.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <functional>
#include <memory>

namespace simdb::argos {

/// \class TimePoint
/// \brief Timestamp snapshot (uint64_t simulation time)
class TimePoint
{
public:
    explicit TimePoint(uint64_t time) :
        time_(time)
    {
    }

    /// Check if the given time point is equal to ours
    bool equals(const TimePoint* time_point, bool must_be_equal_or_less = false) const
    {
        if (!time_point)
        {
            throw DBException("Null time point");
        }
        if (time_ == time_point->time_)
        {
            return true;
        }
        if (must_be_equal_or_less && !lessThan(time_point))
        {
            throw DBException("Time comparison failure: ") << time_ << " <= " << time_point->time_;
        }
        return false;
    }

    /// Check if our time is less than the given time point
    bool lessThan(const TimePoint* time_point) const
    {
        if (!time_point)
        {
            throw DBException("Null time point");
        }
        return time_ < time_point->time_;
    }

    /// Create an entry in the Timestamps table and return the rowid
    int createTimestampInDatabase(DatabaseManager* db_mgr) const
    {
        // Ensure we don't create multiple entries in the Timestamps table
        // that have the same Timestamp column value (it will throw; must
        // be unique).
        //
        // Note that this is called on the DB thread and it is not a big
        // performance issue to query alongside the INSERT.
        auto query = db_mgr->createQuery("Timestamps");
        query->addConstraintForUInt64("Timestamp", Constraints::EQUAL, time_);

        int id;
        query->select("Id", id);

        if (query->getResultSet().getNextRecord())
        {
            assert(id > 0);
            return id;
        }

        return db_mgr->INSERT(SQL_TABLE("Timestamps"), SQL_VALUES(time_))->getId();
    }

    /// Write the time value to the PreparedINSERT at the given column index.
    void assign(PreparedINSERT* inserter, uint32_t col_idx) const { inserter->setColumnValue(col_idx, time_); }

private:
    const uint64_t time_;
};

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

    /// Add the time column in the given table
    void addTimeColumn(Table& tbl, const std::string& col_name = "Timestamp") const
    {
        tbl.addColumn(col_name, SqlDataType::uint64_t);
    }

    /// Store the current time value
    std::shared_ptr<TimePoint> snapshot() const
    {
        uint64_t time = 0;
        if (backpointer_)
        {
            time = *backpointer_;
        } else if (cfuncpointer_)
        {
            time = cfuncpointer_();
        } else
        {
            time = stdfunction_();
        }
        return std::make_shared<TimePoint>(time);
    }

private:
    const uint64_t* backpointer_ = nullptr;
    uint64_t (*cfuncpointer_)() = nullptr;
    std::function<uint64_t()> stdfunction_ = nullptr;
};

} // namespace simdb::argos
