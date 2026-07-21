// <Connection.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/Constraints.hpp"
#include "simdb/sqlite/Transaction.hpp"
#include "simdb/utils/FloatCompare.hpp"

#include <fstream>
#include <memory>
#include <sqlite3.h>
#include <string>

namespace simdb {

/// Callback which gets invoked during SELECT queries that involve
/// floating point comparisons with a supplied tolerance.
inline void fuzzyMatch(sqlite3_context* context, int, sqlite3_value** argv)
{
    const double column_value = sqlite3_value_double(argv[0]);
    const double target_value = sqlite3_value_double(argv[1]);
    const int constraint = sqlite3_value_int(argv[2]);
    static constexpr double tolerance = std::numeric_limits<double>::epsilon();

    simdb_assert(constraint < static_cast<int>(SetConstraints::IN_SET),
                 "Invalid constraint in fuzzyMatch(). Should be Constraints enum.");

    const Constraints e_constraint = static_cast<Constraints>(constraint);

    auto set_is_match = [context](const bool match) { sqlite3_result_int(context, match ? 1 : 0); };

    auto check_equal = [=](const bool should_be_equal) {
        const bool approx_equal = approximatelyEqual(column_value, target_value, tolerance);
        if (approx_equal == should_be_equal)
        {
            set_is_match(true);
        } else
        {
            set_is_match(false);
        }
    };

    switch (e_constraint)
    {
    case Constraints::EQUAL: {
        check_equal(true);
        break;
    }
    case Constraints::NOT_EQUAL: {
        check_equal(false);
        break;
    }
    case Constraints::LESS: {
        set_is_match(column_value < target_value);
        break;
    }
    case Constraints::LESS_EQUAL: {
        if (column_value < target_value)
        {
            set_is_match(true);
            break;
        } else
        {
            check_equal(true);
        }
        break;
    }
    case Constraints::GREATER: {
        set_is_match(column_value > target_value);
        break;
    }
    case Constraints::GREATER_EQUAL: {
        if (column_value > target_value)
        {
            set_is_match(true);
        } else
        {
            check_equal(true);
        }
        break;
    }
    case Constraints::__NUM_CONSTRAINTS__: {
        throw DBException("Invalid constraint in fuzzyMatch()");
    }
    }
}

/*!
 * \class Connection
 *
 * \brief This class instantiates the SQLite schema and issues database
 * commands.
 */
class Connection : public Transaction
{
public:
    /// Close the sqlite3 connection.
    ~Connection()
    {
        if (db_conn_)
        {
            if (sqlite3_close(db_conn_) != SQLITE_OK)
            {
                // Failed to close the database connection. Not much we can do
                // here.
                std::cerr << "Warning: Failed to close database connection: " << sqlite3_errmsg(db_conn_) << std::endl;
            }
        }
    }

    /// Instantiate tables, columns, indexes, etc. on the sqlite3 connection.
    void realizeSchema(const Schema& schema)
    {
        safeTransaction([&]() {
            for (const auto& table : schema.getTables())
            {
                // First create the table and its columns
                std::ostringstream oss;
                oss << "CREATE TABLE IF NOT EXISTS " << table.getName() << "(";

                // Fill in the rest of the CREATE TABLE command
                oss << getColumnsSqlCommand_(table) << ");";

                // Create the table in the database
                executeCommand(oss.str());

                // Now create any table indexes
                for (const auto& cmd : table.index_creation_strs_)
                {
                    executeCommand(cmd);
                }
            }
        });
    }

    /// Get the full database filename being used.
    const std::string& getDatabaseFilePath() const { return db_filepath_; }

    /// Is this connection alive and well?
    bool isValid() const { return (db_conn_ != nullptr); }

    /// Execute the provided statement against the database
    /// connection. This will validate the command, and throw
    /// if this command is disallowed.
    void executeCommand(const std::string_view command)
    {
        auto rc = SQLiteReturnCode(sqlite3_exec(db_conn_, command.data(), nullptr, nullptr, nullptr));
        simdb_assert(!rc, sqlite3_errmsg(db_conn_));
    }

    /// Turn the given command into an SQL prepared statement.
    SQLitePreparedStatement prepareStatement(const std::string_view command)
    {
        return SQLitePreparedStatement(db_conn_, command);
    }

    /// Get the database ID of the last INSERT statement.
    int getLastInsertRowId() const { return sqlite3_last_insert_rowid(db_conn_); }

    /// Get direct access to the underlying SQLite database.
    sqlite3* getDatabase() const { return db_conn_; }

private:
    /// Private constructor. Called by friend class DatabaseManager.
    Connection() {}

    /// First-time database file open.
    std::string openDbFile_(const std::string& db_file)
    {
        db_filepath_ = resolveDbFilename_(db_file);
        if (db_filepath_.empty())
        {
            db_filepath_ = db_file;
        }

        const int db_open_flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
        sqlite3* sqlite_conn = nullptr;
        auto err_code = sqlite3_open_v2(db_filepath_.c_str(), &sqlite_conn, db_open_flags, 0);

        simdb_assert(err_code == SQLITE_OK, "Unable to connect to the database file: " << db_file);

        if (!validateConnectionIsSQLite_(sqlite_conn))
        {
            sqlite3_close(sqlite_conn);
            sqlite_conn = nullptr;
        }

        if (sqlite_conn)
        {
            db_conn_ = sqlite_conn;
            sqlite3_create_function(db_conn_, "fuzzyMatch", 3, SQLITE_UTF8, nullptr, &fuzzyMatch, nullptr, nullptr);
            return db_filepath_;
        } else
        {
            db_conn_ = nullptr;
            return "";
        }
    }

    /// Return a string that is used as part of the CREATE TABLE command:
    /// First TEXT, Last TEXT, Age INT, Balance REAL DEFAULT 50.00
    std::string getColumnsSqlCommand_(const Table& table) const
    {
        std::ostringstream oss;

        // If the table's primary key is "Id", then add that to the schema
        // as the first column.
        const auto& pkey = table.getPrimaryKey();
        if (pkey == "Id")
        {
            oss << "Id INTEGER PRIMARY KEY AUTOINCREMENT, ";
        }

        const auto& columns = table.getColumns();
        for (size_t idx = 0; idx < columns.size(); ++idx)
        {
            auto column = columns[idx];
            oss << column->getName() << " " << column->getDataType();
            if (column->getName() == pkey)
            {
                assert(pkey != "Id");
                oss << " PRIMARY KEY";
            }
            if (column->hasDefaultValue())
            {
                oss << " DEFAULT " << column->getDefaultValueAsString();
            }
            if (column->isUnique() && column->getName() != pkey)
            {
                oss << " UNIQUE";
            }
            if (idx != columns.size() - 1)
            {
                oss << ", ";
            }
        }

        return oss.str();
    }

    // See if there is an existing file by the name <dir/file>
    // and return it. If not, return just <file> if it exists.
    // Return "" if neither could be found.
    std::string resolveDbFilename_(const std::string& db_file) const
    {
        std::ifstream fin(db_file);
        return fin.good() ? db_file : "";
    }

    /// Attempt to run an SQL command against our open connection.
    bool validateConnectionIsSQLite_(sqlite3* db_conn)
    {
        const auto command = "SELECT name FROM sqlite_master WHERE type='table'";
        auto rc = SQLiteReturnCode(sqlite3_exec(db_conn, command, nullptr, nullptr, nullptr));
        return rc == SQLITE_OK;
    }

    /// Filename of the database in use
    std::string db_filepath_;

    friend class DatabaseManager;
};

} // namespace simdb
