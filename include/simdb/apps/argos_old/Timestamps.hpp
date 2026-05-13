// <Timestamps.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/DatabaseManager.hpp"
#include "simdb/utils/ValidValue.hpp"

namespace simdb::collection {

/// \class TimePointBase
/// \brief Type-agnostic base class which holds onto timestamp snapshots
class TimePointBase
{
public:
    /// Apply the stored type-specific time value to the INSERT at column 0
    virtual void apply(PreparedINSERT* inserter) const = 0;

    /// Check if the given time point is equal to ours (dynamic cast must succeed)
    virtual bool equals(const TimePointBase* time_point, bool must_be_equal_or_less = false) const = 0;

    /// Check if our time is less than the given time point (dynamic cast must succeed)
    virtual bool lessThan(const TimePointBase* time_point) const = 0;

    /// Create an entry in the Timestamps table and return the rowid
    virtual int createTimestampInDatabase(DatabaseManager* db_mgr) const = 0;

    /// Stringify the current time value
    virtual std::string getTimeAsString() const = 0;
};

/// \class TimePoint
/// \brief Type-specific timestamp snapshot
template <typename TimeT> class TimePoint : public TimePointBase
{
public:
    explicit TimePoint(const TimeT time) : time_(time) {}

    /// Apply the stored type-specific time value to the INSERT at column 0
    void apply(PreparedINSERT* inserter) const override final
    {
        inserter->setColumnValue(0, time_);
    }

    /// Check if the given time point is equal to ours (dynamic cast must succeed)
    bool equals(const TimePointBase* time_point, bool must_be_equal_or_less = false) const override final
    {
        if (auto typed_time_point = dynamic_cast<const TimePoint<TimeT>*>(time_point))
        {
            if (time_ == typed_time_point->time_)
            {
                return true;
            }
            else if (must_be_equal_or_less && !lessThan(typed_time_point))
            {
                throw DBException("Time comparison failure: ")
                    << time_ << " <= " << typed_time_point->time_;
            }
            return false;
        }
        throw DBException("Dynamic cast failed");
    }

    /// Check if our time is less than the given time point (dynamic cast must succeed)
    bool lessThan(const TimePointBase* time_point) const override final
    {
        if (auto typed_time_point = dynamic_cast<const TimePoint<TimeT>*>(time_point))
        {
            return time_ < typed_time_point->time_;
        }
        throw DBException("Dynamic cast failed");
    }

    /// Create an entry in the Timestamps table and return the rowid
    int createTimestampInDatabase(DatabaseManager* db_mgr) const override final
    {
        // Ensure we don't create multiple entries in the Timestamps table
        // that have the same Timestamp column value (it will throw; must
        // be unique).
        //
        // Note that this is called on the DB thread and it is not a big
        // performance issue to query alongside the INSERT.
        auto query = db_mgr->createQuery("Timestamps");
        if constexpr (std::is_same_v<TimeT, uint64_t>)
        {
            query->addConstraintForUInt64("Timestamp", Constraints::EQUAL, time_);
        }
        else if constexpr (std::is_integral_v<TimeT>)
        {
            auto time = static_cast<int64_t>(time_);
            query->addConstraintForInt64("Timestamp", Constraints::EQUAL, time);
        }
        else
        {
            static_assert(std::is_floating_point_v<TimeT>);
            query->addConstraintForDouble("Timestamp", Constraints::EQUAL, time_);
        }

        int id;
        query->select("Id", id);

        if (query->getResultSet().getNextRecord())
        {
            assert(id > 0);
            return id;
        }

        return db_mgr->INSERT(SQL_TABLE("Timestamps"), SQL_VALUES(time_))->getId();
    }

    /// Stringify the current time value
    std::string getTimeAsString() const override final
    {
        return std::to_string(time_);
    }

private:
    const TimeT time_;
};

/// \class Timestamp
/// \brief Type-specific timestamp that can get current time values via
/// a backpointer, C-style function, or a std::function
template <typename TimeT> class Timestamp
{
public:
    /// \brief Construct with a backpointer to get the current time value
    Timestamp(const TimeT* backpointer) :
        backpointer_(backpointer)
    {
    }

    /// \brief Construct with a C-style function pointer to get the current time value
    Timestamp(TimeT (*fn)()) :
        cfuncpointer_(fn)
    {
    }

    /// \brief Construct with a std::function to get the current time value
    Timestamp(std::function<TimeT()> fn) :
        stdfunction_(fn)
    {
    }

    /// Add the type-specific time column in the given table
    void addTimeColumn(Table& tbl, const std::string& tbl_name = "Timestamp") const
    {
        using dt = SqlDataType;
        if constexpr (std::is_integral_v<TimeT>)
        {
            static_assert(std::is_unsigned_v<TimeT>, "Signed int timestamps not supported");
            if constexpr (std::is_same_v<TimeT, uint64_t>)
            {
                tbl.addColumn(tbl_name, dt::uint64_t);
            } else
            {
                tbl.addColumn(tbl_name, dt::uint32_t);
            }
        } else
        {
            tbl.addColumn(tbl_name, dt::double_t);
        }
    }

    /// Store the current type-specific time value
    std::shared_ptr<TimePointBase> snapshot() const
    {
        TimeT time = 0;
        if (backpointer_)
        {
            time = *backpointer_;
        }
        else if (cfuncpointer_)
        {
            time = cfuncpointer_();
        }
        else
        {
            time = stdfunction_();
        }
        return std::make_shared<TimePoint<TimeT>>(time);
    }

    std::string getTimeAsString() const
    {
        return snapshot()->getTimeAsString();
    }

private:
    const TimeT* backpointer_ = nullptr;
    TimeT (*cfuncpointer_)() = nullptr;
    std::function<TimeT()> stdfunction_ = nullptr;
    mutable ValidValue<TimeT> time_;
};

} // namespace simdb::collection
