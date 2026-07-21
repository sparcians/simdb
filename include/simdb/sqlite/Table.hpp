// <Table.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/sqlite/Query.hpp"
#include "simdb/sqlite/Transaction.hpp"
#include "simdb/sqlite/ValueContainer.hpp"
#include "simdb/utils/utf16.hpp"

#include <algorithm>
#include <list>
#include <sqlite3.h>

namespace simdb {

/*!
 * \class SqlTable
 *
 * \brief Helper class that is used under the hood for
 * db_mgr.INSERT(SQL_TABLE("MyTable"), ...)
 */
class SqlTable
{
public:
    SqlTable(const std::string_view table_name) :
        table_name_(table_name)
    {
    }

    const std::string& getName() const { return table_name_; }

private:
    std::string table_name_;
};

/*!
 * \class SqlColumns
 *
 * \brief Helper class that is used under the hood for db_mgr.INSERT(...,
 * SQL_COLUMNS("ColA", "ColB"), ...)
 */
class SqlColumns
{
public:
    template <typename... Rest>
    SqlColumns(const char* col_name, Rest... rest) :
        SqlColumns(std::forward<Rest>(rest)...)
    {
        col_names_.emplace_front(col_name);
    }

    SqlColumns(const char* col_name)
    {
        // It is important that this is emplace_front()
        // since this constructor might be called last
        // in a chain of constructors (parameter pack
        // unrolling).
        col_names_.emplace_front(col_name);
    }

    SqlColumns(const std::vector<std::string>& col_names)
    {
        for (const auto& col_name : col_names)
        {
            col_names_.emplace_back(col_name);
        }
    }

    void writeColsForINSERT(std::ostringstream& oss) const
    {
        oss << " (";
        auto iter = col_names_.begin();
        size_t idx = 0;
        while (iter != col_names_.end())
        {
            oss << *iter;
            if (idx != col_names_.size() - 1)
            {
                oss << ",";
            }
            ++idx;
            ++iter;
        }
        oss << ") ";
    }

    const std::list<std::string>& getColNames() const { return col_names_; }

private:
    std::list<std::string> col_names_;
};

/*!
 * \class SqlValues
 *
 * \brief Helper class that is used together with SQL_VALUES() in order to bind
 * data values to a prepared statement for INSERT's.
 */
class SqlValues
{
public:
    template <typename T, typename... Rest>
    SqlValues(T val, Rest... rest) :
        SqlValues(std::forward<Rest>(rest)...)
    {
        col_vals_.emplace_front(createValueContainer<T>(val));
    }

    template <typename T, typename... Rest>
    SqlValues(const std::vector<T>& val, Rest... rest) :
        SqlValues(std::forward<Rest>(rest)...)
    {
        col_vals_.emplace_front(createValueContainer(val));
    }

    template <typename T, typename... Rest>
    SqlValues(std::vector<T>&& val, Rest... rest) :
        SqlValues(std::forward<Rest>(rest)...)
    {
        col_vals_.emplace_front(createValueContainer(std::move(val)));
    }

    template <typename T> SqlValues(T val) { col_vals_.emplace_front(createValueContainer<T>(val)); }

    template <typename T> SqlValues(const std::vector<T>& val) { col_vals_.emplace_front(createValueContainer(val)); }

    template <typename T> SqlValues(std::vector<T>&& val)
    {
        col_vals_.emplace_front(createValueContainer(std::move(val)));
    }

    void writeValsForINSERT(std::ostringstream& oss) const
    {
        oss << " VALUES(";
        size_t count = col_vals_.size();
        for (size_t idx = 0; idx < count; ++idx)
        {
            oss << "?";
            if (idx != count - 1)
            {
                oss << ",";
            }
        }
        oss << ") ";
    }

    void bindValsForINSERT(sqlite3_stmt* stmt) const
    {
        int32_t idx = 1;
        for (auto& val : col_vals_)
        {
            auto rc = SQLiteReturnCode(val->bind(stmt, idx++));
            simdb_assert(!rc, sqlite3_errmsg(sqlite3_db_handle(stmt)));
        }
    }

private:
    std::list<std::shared_ptr<ValueContainerBase>> col_vals_;
};

/*!
 * \class SqlRecord
 *
 * \brief This class wraps one table record by its table name and database ID.
 */
class SqlRecord
{
public:
    SqlRecord(const std::string& table_name, const int32_t db_id, sqlite3* db_conn, Transaction* transaction) :
        table_name_(table_name),
        db_id_(db_id),
        db_conn_(db_conn),
        transaction_(transaction)
    {
    }

    /// Get the database ID (primary key) for this record.
    int32_t getId() const { return db_id_; }

    /// SELECT the given column value (int32_t)
    int32_t getPropertyInt32(const char* col_name) const;

    /// SELECT the given column value (uint32_t)
    uint32_t getPropertyUInt32(const char* col_name) const;

    /// SELECT the given column value (int64_t)
    int64_t getPropertyInt64(const char* col_name) const;

    /// SELECT the given column value (uint64_t)
    uint64_t getPropertyUInt64(const char* col_name) const;

    /// SELECT the given column value (double)
    double getPropertyDouble(const char* col_name) const;

    /// SELECT the given column value (string)
    std::string getPropertyString(const char* col_name) const;

    /// SELECT the given column value (blob)
    template <typename T> std::vector<T> getPropertyBlob(const char* col_name) const;

    /// UPDATE the given column value (int32_t)
    void setPropertyInt32(const char* col_name, const int32_t val) const;

    /// UPDATE the given column value (uint32_t)
    void setPropertyUInt32(const char* col_name, const uint32_t val) const;

    /// UPDATE the given column value (int64_t)
    void setPropertyInt64(const char* col_name, const int64_t val) const;

    /// UPDATE the given column value (uint64_t)
    void setPropertyUInt64(const char* col_name, const uint64_t val) const;

    /// UPDATE the given column value (double)
    void setPropertyDouble(const char* col_name, const double val) const;

    /// UPDATE the given column value (string)
    void setPropertyString(const char* col_name, const std::string& val) const;

    /// UPDATE the given column value (blob)
    template <typename T> void setPropertyBlob(const char* col_name, const std::vector<T>& val) const;

    /// UPDATE the given column value (blob)
    void setPropertyBlob(const char* col_name, const void* data, const size_t bytes) const;

    /// DELETE this record from its table. Returns TRUE if successful,
    /// FALSE otherwise. Should return FALSE on subsequent calls to this method.
    bool removeFromTable();

private:
    /// Create a prepared statement: UPDATE <table_name_> SET <col_name>=? WHERE
    /// Id=<db_id_>
    SQLitePreparedStatement createSetPropertyStmt_(const char* col_name) const
    {
        std::string cmd = "UPDATE " + table_name_;
        cmd += " SET ";
        cmd += col_name;
        cmd += "=? WHERE Id=" + std::to_string(db_id_);

        return SQLitePreparedStatement(db_conn_, cmd);
    }

    /// \brief Step a prepared statement forward
    /// \param stmt Prepared statement
    /// \param ret_codes List of expected return codes from sqlite3_step()
    /// \throws Throws an exception if sqlite3_step() returned a code that was
    /// not in <ret_codes>
    void stepStatement_(sqlite3_stmt* stmt, const std::initializer_list<int>& ret_codes) const
    {
        auto rc = SQLiteReturnCode(sqlite3_step(stmt));
        if (std::find(ret_codes.begin(), ret_codes.end(), rc) == ret_codes.end())
        {
            auto stringify_ret_codes = [&]() {
                std::ostringstream oss;
                auto iter = ret_codes.begin();
                for (size_t idx = 0; idx < ret_codes.size(); ++idx)
                {
                    oss << *iter;
                    if (idx != ret_codes.size() - 1)
                    {
                        oss << ",";
                    }
                    ++iter;
                }
                return oss.str();
            };

            simdb_assert(false, "Unexpected sqlite3_step() return code:\n"
                                    << "\tActual: " << rc << "\tExpected: " << stringify_ret_codes()
                                    << "\tError: " << sqlite3_errmsg(db_conn_));
        }
    }

    // SELECT ColA FROM <table_name_> WHERE Id=<db_id_>
    const std::string table_name_;

    // SELECT ColA FROM <table_name_> WHERE Id=<db_id_>
    const int32_t db_id_;

    // Underlying sqlite3 database
    sqlite3* const db_conn_;

    // Used for safeTransaction()
    Transaction* const transaction_;
};

/// Run a query on the given table, column, and database ID, and return the
/// property value. SELECT <col_name> FROM <table_name> WHERE Id=<db_id>
template <typename T>
inline T queryPropertyValue(const char* table_name, const char* col_name, const int db_id, sqlite3* db_conn)
{
    SqlQuery query(table_name, db_conn);

    // SELECT <col_name> FROM <table_name> WHERE Id=<db_id>
    T val;
    query.select(col_name, val);
    query.addConstraintForInt("Id", Constraints::EQUAL, db_id);

    auto result_set = query.getResultSet();
    simdb_assert(result_set.getNextRecord(), "Record not found");

    return val;
}

inline int32_t SqlRecord::getPropertyInt32(const char* col_name) const
{
    return queryPropertyValue<int32_t>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline uint32_t SqlRecord::getPropertyUInt32(const char* col_name) const
{
    return queryPropertyValue<uint32_t>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline int64_t SqlRecord::getPropertyInt64(const char* col_name) const
{
    return queryPropertyValue<int64_t>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline uint64_t SqlRecord::getPropertyUInt64(const char* col_name) const
{
    return queryPropertyValue<uint64_t>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline double SqlRecord::getPropertyDouble(const char* col_name) const
{
    return queryPropertyValue<double>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline std::string SqlRecord::getPropertyString(const char* col_name) const
{
    return queryPropertyValue<std::string>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

template <typename T> inline std::vector<T> SqlRecord::getPropertyBlob(const char* col_name) const
{
    return queryPropertyValue<std::vector<T>>(table_name_.c_str(), col_name, db_id_, db_conn_);
}

inline void SqlRecord::setPropertyInt32(const char* col_name, const int32_t val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_int(stmt, 1, val)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline void SqlRecord::setPropertyUInt32(const char* col_name, const uint32_t val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(val))),
                     sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline void SqlRecord::setPropertyInt64(const char* col_name, const int64_t val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_int64(stmt, 1, val)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline void SqlRecord::setPropertyUInt64(const char* col_name, const uint64_t val) const
{
    transaction_->safeTransaction([&]() {
        auto utf16 = utils::uint64_to_utf16(val);
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_text16(stmt, 1, utf16.data(), 40, 0)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline void SqlRecord::setPropertyDouble(const char* col_name, const double val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_double(stmt, 1, val)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});

        return true;
    });
}

inline void SqlRecord::setPropertyString(const char* col_name, const std::string& val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_text(stmt, 1, val.c_str(), -1, 0)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

template <typename T> inline void SqlRecord::setPropertyBlob(const char* col_name, const std::vector<T>& val) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_blob(stmt, 1, val.data(), val.size() * sizeof(T), 0)),
                     sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline void SqlRecord::setPropertyBlob(const char* col_name, const void* data, const size_t bytes) const
{
    transaction_->safeTransaction([&]() {
        auto stmt = createSetPropertyStmt_(col_name);
        simdb_assert(!SQLiteReturnCode(sqlite3_bind_blob(stmt, 1, data, bytes, 0)), sqlite3_errmsg(db_conn_));
        stepStatement_(stmt, {SQLITE_DONE});
    });
}

inline bool SqlRecord::removeFromTable()
{
    transaction_->safeTransaction([&]() {
        std::ostringstream oss;
        oss << "DELETE FROM " << table_name_ << " WHERE Id=" << db_id_;
        const auto cmd = oss.str();

        auto rc = SQLiteReturnCode(sqlite3_exec(db_conn_, cmd.c_str(), nullptr, nullptr, nullptr));
        simdb_assert(!rc, sqlite3_errmsg(db_conn_));
    });

    return sqlite3_changes(db_conn_) == 1;
}

} // namespace simdb

#define SQL_TABLE(name) simdb::SqlTable(name)
#define SQL_COLUMNS(...) simdb::SqlColumns(__VA_ARGS__)
#define SQL_VALUES(...) simdb::SqlValues(__VA_ARGS__)
