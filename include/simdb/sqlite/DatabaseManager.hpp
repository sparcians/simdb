// <DatabaseManager.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/Connection.hpp"
#include "simdb/sqlite/PreparedINSERT.hpp"
#include "simdb/sqlite/Query.hpp"
#include "simdb/sqlite/Table.hpp"
#include <filesystem>
#include <set>

namespace simdb {

enum class JournalMode {
    // PRAGMA journal_mode = OFF
    // PRAGMA synchronous = OFF
    FASTEST,

    // PRAGMA journal_mode = DELETE;
    // PRAGMA synchronous = FULL;
    SAFEST,

    // PRAGMA journal_mode = WAL;
    // PRAGMA synchronous = NORMAL;
    BALANCED
};

using PragmaPairs = std::vector<std::pair<std::string, std::string>>;

inline PragmaPairs getPragmas(JournalMode mode)
{
    switch (mode)
    {
    case JournalMode::FASTEST:
        return {{"journal_mode", "OFF"}, {"synchronous", "OFF"}};
    case JournalMode::SAFEST:
        return {{"journal_mode", "DELETE"}, {"synchronous", "FULL"}};
    case JournalMode::BALANCED:
        return {{"journal_mode", "WAL"}, {"synchronous", "NORMAL"}};
    }
}

/*!
 * \class DatabaseManager
 *
 * \brief This class is the primary "entry point" into SimDB connections,
 *        including instantiating schemas, creating records, querying
 *        records, and accessing the underlying Connection.
 */
class DatabaseManager
{
public:
    /// \brief Construct a DatabaseManager
    /// \param db_file Name of the database file, typically with .db extension
    /// \param force_new_file Force the <db_file> to be overwritten if it
    /// exists.
    ///                       If the file already existed and this flag is
    ///                       false, then you will not be able to call
    ///                       appendSchema() on this DatabaseManager as the
    ///                       schema is fixed.
    /// \param pragmas Pragmas to set on the database as soon as it is opened.
    DatabaseManager(const std::string& db_file = "sim.db", const bool force_new_file = false,
                    const PragmaPairs& pragmas = {}) :
        db_file_(db_file)
    {
        auto db_file_exists = std::filesystem::exists(db_file);
        if (db_file_exists && force_new_file)
        {
            const auto cmd = "rm -f " + db_file;
            auto rc = system(cmd.c_str());
            (void)rc;
        }

        db_conn_.reset(new Connection);
        openDatabaseFile_(pragmas);

        if (db_file_exists && !force_new_file)
        {
            try
            {
                connectToExistingDatabase_();
            } catch (const std::exception& ex)
            {
                throw DBException("Unable to connect to database file: ")
                    << db_file << "\n  *** error message: " << ex.what();
            }
        }

        if (!schema_.hasTable("internal$SchemaTables"))
        {
            using dt = SqlDataType;
            Schema schema;

            auto& schema_tables = schema.addTable("internal$SchemaTables");
            schema_tables.addColumn("TableName", dt::string_t);
            schema_tables.addColumn("IndexedColumns", dt::string_t);
            schema_tables.addColumn("PrimaryKey", dt::string_t);
            schema_tables.setColumnDefaultValue("PrimaryKey", "Id");

            auto& schema_columns = schema.addTable("internal$SchemaColumns");
            schema_columns.addColumn("TableName", dt::string_t);
            schema_columns.addColumn("ColumnName", dt::string_t);
            schema_columns.addColumn("ColumnDType", dt::string_t);
            schema_columns.addColumn("ColumnDefaultVal", dt::string_t);
            schema_columns.createIndexOn("TableName");

            appendSchema(schema);
        }
    }

    /// \brief   Add one or more tables to the existing schema.
    ///
    /// \throws  This will throw an exception for DatabaseManager's
    ///          whose connection was initialized with a previously
    ///          existing file.
    ///
    /// \return  Returns true if successful, false otherwise.
    void appendSchema(const Schema& schema)
    {
        schema_.appendSchema(schema);
        db_conn_->realizeSchema(schema);
        serializeSchema_();
    }

    /// Get the schema for this database.
    const Schema& getSchema() const { return schema_; }

    /// Get the full database file path.
    const std::string& getDatabaseFilePath() const { return db_filepath_; }

    /// Execute the functor inside BEGIN/COMMIT TRANSACTION.
    void safeTransaction(const TransactionFunc& func) const { db_conn_->safeTransaction(func); }

    /// For debug purposes only.
    bool isInTransaction() const { return db_conn_ && db_conn_->isInTransaction(); }

    /// \brief  Perform INSERT operation on this database.
    ///
    /// \note   The way to call this method is:
    ///         db_mgr.INSERT(SQL_TABLE("TableName"),
    ///                       SQL_COLUMNS("ColA", "ColB"),
    ///                       SQL_VALUES(3.14, "foo"));
    ///
    /// \note   You may also provide ValueContainerBase subclasses in the
    /// SQL_VALUES.
    ///
    /// \return SqlRecord which wraps the table and the ID of its record.
    std::unique_ptr<SqlRecord> INSERT(SqlTable&& table, SqlColumns&& cols, SqlValues&& vals)
    {
        simdb_assert(table.getName().find("internal$") != 0, "Cannot perform INSERT. This is an internal table.");
        return INSERT_(std::move(table), std::move(cols), std::move(vals));
    }

    /// \brief INSERT into a table with values for all columns, in schema order.
    ///        Equivalent to INSERT(table, SQL_COLUMNS(...), vals) with every
    ///        column of the table. The number and order of values must match
    ///        the table's columns.
    std::unique_ptr<SqlRecord> INSERT(SqlTable&& table, SqlValues&& vals)
    {
        simdb_assert(table.getName().find("internal$") != 0, "Cannot perform INSERT. This is an internal table.");
        std::vector<std::string> col_names;
        for (const auto& col : schema_.getTable(table.getName()).getColumns())
        {
            col_names.push_back(col->getName());
        }
        SqlColumns cols(col_names);
        return INSERT_(std::move(table), std::move(cols), std::move(vals));
    }

    /// This INSERT() overload is to be used for tables that were defined with
    /// at least one default value for its column(s).
    std::unique_ptr<SqlRecord> INSERT(SqlTable&& table)
    {
        simdb_assert(table.getName().find("internal$") != 0, "Cannot perform INSERT. This is an internal table.");
        return INSERT_(std::move(table));
    }

    /// \brief Create a prepared statement for faster record creation
    ///        when you are creating many of the same table records.
    std::unique_ptr<PreparedINSERT> prepareINSERT(SqlTable&& table, SqlColumns&& cols)
    {
        simdb_assert(table.getName().find("internal$") != 0, "Cannot perform INSERT. This is an internal table.");
        return prepareINSERT_(std::move(table), std::move(cols));
    }

    /// \brief Create a prepared statement for inserting into all columns of a table.
    ///        Equivalent to prepareINSERT(table, SQL_COLUMNS(...)) with every column
    ///        of the table, in schema order.
    std::unique_ptr<PreparedINSERT> prepareINSERT(SqlTable&& table)
    {
        simdb_assert(table.getName().find("internal$") != 0, "Cannot perform INSERT. This is an internal table.");
        std::vector<std::string> col_names;
        for (const auto& col : schema_.getTable(table.getName()).getColumns())
        {
            col_names.push_back(col->getName());
        }
        SqlColumns cols(col_names);
        return prepareINSERT_(std::move(table), std::move(cols));
    }

    /// \brief Execute an arbitrary SQL command on this database.
    void EXECUTE(const std::string& sql_cmd, bool in_transaction = true)
    {
        std::vector<std::string> args;
        boost::split(args, sql_cmd, boost::is_any_of(" "));
        if (args.size() >= 3)
        {
            std::transform(args[0].begin(), args[0].end(), args[0].begin(),
                           [](unsigned char ch) { return std::tolower(ch); });

            std::transform(args[1].begin(), args[1].end(), args[1].begin(),
                           [](unsigned char ch) { return std::tolower(ch); });

            simdb_assert(!(args[0] == "insert" && args[1] == "into" && args[2].find("internal$") == 0),
                         "Invalid command. Cannot write to internal tables. Command: " << sql_cmd);
        }

        if (in_transaction)
        {
            db_conn_->safeTransaction([&]() {
                auto rc =
                    SQLiteReturnCode(sqlite3_exec(db_conn_->getDatabase(), sql_cmd.c_str(), nullptr, nullptr, nullptr));
                simdb_assert(!rc, sqlite3_errmsg(db_conn_->getDatabase()));
            });
        } else
        {
            auto rc =
                SQLiteReturnCode(sqlite3_exec(db_conn_->getDatabase(), sql_cmd.c_str(), nullptr, nullptr, nullptr));
            simdb_assert(!rc, sqlite3_errmsg(db_conn_->getDatabase()));
        }
    }

    /// \brief  Get a SqlRecord from a database ID for the given table.
    ///
    /// \return Returns the record wrapper if found, or nullptr if not.
    std::unique_ptr<SqlRecord> findRecord(const char* table_name, const int db_id) const
    {
        return findRecord_(table_name, db_id, false);
    }

    /// \brief  Get a SqlRecord from a database ID for the given table.
    ///
    /// \throws Throws an exception if this database ID is not found in the
    /// given table.
    std::unique_ptr<SqlRecord> getRecord(const char* table_name, const int db_id) const
    {
        return findRecord_(table_name, db_id, true);
    }

    /// \brief  Delete one record from the given table with the given ID.
    ///
    /// \return Returns true if successful, false otherwise.
    bool removeRecordFromTable(const char* table_name, const int db_id)
    {
        db_conn_->safeTransaction([&]() {
            std::ostringstream oss;
            oss << "DELETE FROM " << table_name << " WHERE Id=" << db_id;
            const auto cmd = oss.str();

            auto rc = SQLiteReturnCode(sqlite3_exec(db_conn_->getDatabase(), cmd.c_str(), nullptr, nullptr, nullptr));
            simdb_assert(!rc, sqlite3_errmsg(db_conn_->getDatabase()));
        });

        return sqlite3_changes(db_conn_->getDatabase()) == 1;
    }

    /// \brief  Delete every record from the given table.
    ///
    /// \return Returns the total number of deleted records.
    uint32_t removeAllRecordsFromTable(const char* table_name)
    {
        db_conn_->safeTransaction([&]() {
            std::ostringstream oss;
            oss << "DELETE FROM " << table_name;
            const auto cmd = oss.str();

            auto rc = SQLiteReturnCode(sqlite3_exec(db_conn_->getDatabase(), cmd.c_str(), nullptr, nullptr, nullptr));
            simdb_assert(!rc, sqlite3_errmsg(db_conn_->getDatabase()));
        });

        return sqlite3_changes(db_conn_->getDatabase());
    }

    /// \brief  Issue "DELETE FROM TableName" to clear out the given table.
    ///
    /// \return Returns the total number of deleted records across all tables.
    uint32_t removeAllRecordsFromAllTables()
    {
        uint32_t count = 0;

        db_conn_->safeTransaction([&]() {
            const char* cmd = "SELECT name FROM sqlite_master WHERE type='table'";
            auto stmt = db_conn_->prepareStatement(cmd);

            while (true)
            {
                auto rc = SQLiteReturnCode(sqlite3_step(stmt));
                if (rc != SQLITE_ROW)
                {
                    break;
                }

                auto table_name = sqlite3_column_text(stmt, 0);
                count += removeAllRecordsFromTable((const char*)table_name);
            }
        });

        return count;
    }

    /// Get a query object to issue SELECT statements with constraints.
    std::unique_ptr<SqlQuery> createQuery(const char* table_name)
    {
        return std::unique_ptr<SqlQuery>(new SqlQuery(table_name, db_conn_->getDatabase()));
    }

private:
    /// \brief  Open a database connection to an existing database file.
    ///
    /// \param db_fpath Full path to the database including the ".db"
    ///                 extension, e.g. "/path/to/my/dir/statistics.db"
    ///
    /// \note   The 'db_fpath' is typically one that was given to us
    ///         from a previous call to getDatabaseFilePath()
    ///
    /// \return Returns error message if failed to connect
    void connectToExistingDatabase_()
    {
        assert(db_conn_ != nullptr);
        reconstituteSchema_();
        db_filepath_ = db_conn_->getDatabaseFilePath();
    }

    /// \brief Regenerate the Schema object when attaching to an existing database file.
    void reconstituteSchema_()
    {
        assert(schema_.getTables().empty());

        auto tbl_query = createQuery("internal$SchemaTables");

        std::string table_name, indexed_columns, primary_key;
        tbl_query->select("TableName", table_name);
        tbl_query->select("IndexedColumns", indexed_columns);
        tbl_query->select("PrimaryKey", primary_key);

        auto tbl_results = tbl_query->getResultSet();
        while (tbl_results.getNextRecord())
        {
            auto& tbl = schema_.addTable(table_name);

            auto col_query = createQuery("internal$SchemaColumns");
            col_query->addConstraintForString("TableName", Constraints::EQUAL, table_name);

            std::string col_name, dtype_str, default_val_str;
            col_query->select("ColumnName", col_name);
            col_query->select("ColumnDType", dtype_str);
            col_query->select("ColumnDefaultVal", default_val_str);

            auto col_results = col_query->getResultSet();
            while (col_results.getNextRecord())
            {
                using dt = SqlDataType;
                static std::unordered_map<std::string, dt> dtypes;
                if (dtypes.empty())
                {
                    dtypes["int32_t"] = dt::int32_t;
                    dtypes["uint32_t"] = dt::uint32_t;
                    dtypes["int64_t"] = dt::int64_t;
                    dtypes["uint64_t"] = dt::uint64_t;
                    dtypes["double_t"] = dt::double_t;
                    dtypes["string_t"] = dt::string_t;
                    dtypes["blob_t"] = dt::blob_t;
                }

                tbl.addColumn(col_name, dtypes.at(dtype_str));
                if (!default_val_str.empty())
                {
                    tbl.getColumn_(col_name).setDefaultValueStr_(default_val_str);
                }
            }

            // Indexed columns could be:
            //   foo,bar|biz
            // Which means:
            //   tbl.createCompoundIndexOn({"foo","bar"});
            //   tbl.createIndexOn("biz");
            if (!indexed_columns.empty())
            {
                std::vector<std::string> index_strs;
                boost::split(index_strs, indexed_columns, boost::is_any_of("|"));
                for (const auto& index_str : index_strs)
                {
                    std::vector<std::string> index_cols;
                    boost::split(index_cols, index_str, boost::is_any_of(","));
                    if (index_cols.size() == 1)
                    {
                        tbl.createIndexOn(index_cols[0]);
                    } else
                    {
                        tbl.createCompoundIndexOn(index_cols);
                    }
                }
            }

            tbl.setPrimaryKey(primary_key);
        }
    }

    /// Open the given database file.
    bool openDatabaseFile_(const PragmaPairs& pragmas)
    {
        if (!db_conn_)
        {
            return false;
        }

        auto db_filename = db_conn_->openDbFile_(db_file_);
        if (!db_filename.empty())
        {
            // File opened without issues. Store the full DB filename.
            db_filepath_ = db_filename;

            // Issue PRAGMA's
            for (const auto& [name, val] : pragmas)
            {
                EXECUTE("PRAGMA " + name + " = " + val, false);
            }

            return true;
        }

        return false;
    }

    /// Serialize everything we know about the schema to the database.
    void serializeSchema_()
    {
        safeTransaction([&]() {
            removeAllRecordsFromTable("internal$SchemaTables");
            removeAllRecordsFromTable("internal$SchemaColumns");

            auto tbls_inserter = prepareINSERT_(SQL_TABLE("internal$SchemaTables"),
                                                SQL_COLUMNS("TableName", "IndexedColumns", "PrimaryKey"));

            auto cols_inserter =
                prepareINSERT_(SQL_TABLE("internal$SchemaColumns"),
                               SQL_COLUMNS("TableName", "ColumnName", "ColumnDType", "ColumnDefaultVal"));

            auto commaSepStringList = [](const std::vector<std::string>& strings) {
                std::ostringstream oss;
                for (size_t i = 0; i < strings.size(); ++i)
                {
                    oss << strings[i];
                    if (i != strings.size() - 1)
                    {
                        oss << ",";
                    }
                }
                return oss.str();
            };

            for (const auto& table : schema_.getTables())
            {
                const auto& table_name = table.getName();

                std::ostringstream indexes_oss;
                for (const auto& index_cols : table.getTableIndexes())
                {
                    indexes_oss << commaSepStringList(index_cols) << "|";
                }

                auto indexed_cols = indexes_oss.str();
                if (!indexed_cols.empty())
                {
                    indexed_cols.pop_back();
                }

                auto primary_key = table.getPrimaryKey();

                tbls_inserter->setColumnValue(0, table_name);
                tbls_inserter->setColumnValue(1, indexed_cols);
                tbls_inserter->setColumnValue(2, primary_key);
                tbls_inserter->createRecord();

                for (const auto& column : table.getColumns())
                {
                    using dt = SqlDataType;
                    static std::unordered_map<dt, std::string> dtype_strs;
                    if (dtype_strs.empty())
                    {
                        dtype_strs[dt::int32_t] = "int32_t";
                        dtype_strs[dt::uint32_t] = "uint32_t";
                        dtype_strs[dt::int64_t] = "int64_t";
                        dtype_strs[dt::uint64_t] = "uint64_t";
                        dtype_strs[dt::double_t] = "double_t";
                        dtype_strs[dt::string_t] = "string_t";
                        dtype_strs[dt::blob_t] = "blob_t";
                    }

                    const auto& column_name = column->getName();
                    const auto column_dtype_str = dtype_strs.at(column->getDataType());
                    const auto column_default_str = column->getDefaultValueAsString();

                    cols_inserter->setColumnValue(0, table_name);
                    cols_inserter->setColumnValue(1, column_name);
                    cols_inserter->setColumnValue(2, column_dtype_str);
                    cols_inserter->setColumnValue(3, column_default_str);
                    cols_inserter->createRecord();
                }
            }
        });
    }

    /// \see See public INSERT() method for details on how to call this method.
    std::unique_ptr<SqlRecord> INSERT_(SqlTable&& table, SqlColumns&& cols, SqlValues&& vals)
    {
        std::unique_ptr<SqlRecord> record;

        db_conn_->safeTransaction([&]() {
            std::ostringstream oss;
            oss << "INSERT INTO " << table.getName();
            cols.writeColsForINSERT(oss);
            vals.writeValsForINSERT(oss);

            std::string cmd = oss.str();
            auto stmt = db_conn_->prepareStatement(cmd);
            vals.bindValsForINSERT(stmt);

            auto rc = SQLiteReturnCode(sqlite3_step(stmt));
            simdb_assert(rc == SQLITE_DONE,
                         "Could not perform INSERT. Error: " << sqlite3_errmsg(db_conn_->getDatabase()));

            auto db_id = db_conn_->getLastInsertRowId();
            record.reset(new SqlRecord(table.getName(), db_id, db_conn_->getDatabase(), db_conn_.get()));
        });

        return record;
    }

    /// \see See public INSERT() method for details on how to call this method.
    std::unique_ptr<SqlRecord> INSERT_(SqlTable&& table)
    {
        std::unique_ptr<SqlRecord> record;

        db_conn_->safeTransaction([&]() {
            const std::string cmd = "INSERT INTO " + table.getName() + " DEFAULT VALUES";
            auto stmt = db_conn_->prepareStatement(cmd);

            auto rc = SQLiteReturnCode(sqlite3_step(stmt));
            simdb_assert(rc == SQLITE_DONE,
                         "Could not perform INSERT. Error: " << sqlite3_errmsg(db_conn_->getDatabase()));

            auto db_id = db_conn_->getLastInsertRowId();
            record.reset(new SqlRecord(table.getName(), db_id, db_conn_->getDatabase(), db_conn_.get()));
        });

        return record;
    }

    /// \see See public prepareINSERT() method for details on how to call this method.
    std::unique_ptr<PreparedINSERT> prepareINSERT_(SqlTable&& table, SqlColumns&& cols)
    {
        const std::set<std::string> col_names(cols.getColNames().begin(), cols.getColNames().end());

        std::vector<SqlDataType> col_dtypes;
        for (const auto& col : schema_.getTable(table.getName()).getColumns())
        {
            if (col_names.count(col->getName()))
            {
                col_dtypes.push_back(col->getDataType());
            }
        }

        std::ostringstream oss;
        oss << "INSERT INTO " << table.getName() << " (";

        auto it = cols.getColNames().begin();
        while (true)
        {
            oss << *it;
            if (++it != cols.getColNames().end())
            {
                oss << ",";
            } else
            {
                break;
            }
        }

        oss << ") VALUES (";
        for (size_t i = 0; i < col_dtypes.size(); ++i)
        {
            oss << "?" << (i != col_dtypes.size() - 1 ? "," : "");
        }
        oss << ")";

        std::string cmd = oss.str();
        auto stmt = db_conn_->prepareStatement(cmd);

        return std::make_unique<PreparedINSERT>(std::move(stmt), col_dtypes, db_conn_);
    }

    /// Get a SqlRecord from a database ID for the given table.
    std::unique_ptr<SqlRecord> findRecord_(const char* table_name, const int db_id, const bool must_exist) const
    {
        std::ostringstream oss;
        oss << "SELECT * FROM " << table_name << " WHERE Id=" << db_id;
        const auto cmd = oss.str();

        auto stmt = SQLitePreparedStatement(db_conn_->getDatabase(), cmd);
        auto rc = SQLiteReturnCode(sqlite3_step(stmt));

        simdb_assert(!(must_exist && rc == SQLITE_DONE),
                     "Record not found with ID " << db_id << " in table " << table_name);
        if (rc == SQLITE_DONE)
        {
            return nullptr;
        }
        if (rc == SQLITE_ROW)
        {
            return std::unique_ptr<SqlRecord>(
                new SqlRecord(table_name, db_id, db_conn_->getDatabase(), db_conn_.get()));
        }
        throw DBException("Internal error has occured: ") << sqlite3_errmsg(db_conn_->getDatabase());
    }

    /// Database connection.
    std::shared_ptr<Connection> db_conn_;

    /// Schema for this database.
    Schema schema_;

    /// Name of the database file.
    const std::string db_file_;

    /// Full database file name, including the database path
    /// and file extension
    std::string db_filepath_;
};

} // namespace simdb
