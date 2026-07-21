// <SchemaDef.hpp> -*- C++ -*-

#pragma once

#include "simdb/Assert.hpp"
#include "simdb/Exceptions.hpp"

#include <algorithm>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/algorithm/string.hpp>

namespace simdb {

class DatabaseManager;

/// Data types supported by SimDB schemas
enum class SqlDataType { int32_t, uint32_t, int64_t, uint64_t, double_t, string_t, blob_t };

/// Stream operator used when creating various SQL commands.
inline std::ostream& operator<<(std::ostream& os, const SqlDataType dtype)
{
    using dt = SqlDataType;

    switch (dtype)
    {
    case dt::int32_t:
    case dt::uint32_t:
    case dt::int64_t: {
        os << "INT";
        break;
    }

    case dt::string_t:
    case dt::uint64_t: {
        os << "TEXT";
        break;
    }

    case dt::double_t: {
        os << "REAL";
        break;
    }

    case dt::blob_t: {
        os << "BLOB";
        break;
    }
    }

    return os;
}

/*!
 * \class Column
 *
 * \brief This class is used for creating SimDB tables.
 */
class Column
{
public:
    /// Construct with the column name and data type.
    Column(const std::string& column_name, const SqlDataType dt) :
        name_(column_name),
        dt_(dt)
    {
    }

    /// Equivalence is defined as having the same name and data type.
    bool operator==(const Column& rhs) const
    {
        return name_ == rhs.name_ && dt_ == rhs.dt_ && default_val_string_ == rhs.default_val_string_;
    }

    /// Equivalence is defined as having the same name and data type.
    bool operator!=(const Column& rhs) const { return !(*this == rhs); }

    /// Get the name of this column.
    const std::string& getName() const { return name_; }

    /// Get the data type of this column.
    SqlDataType getDataType() const { return dt_; }

    /// Optionally specify a default value for this Column.
    /// Defaults for SqlBlob data types are not allowed and will
    /// throw if you attempt to set a SqlBlob default value.
    template <typename T> void setDefaultValue(const T val)
    {
        simdb_assert(dt_ != SqlDataType::blob_t, "Cannot set default value for a database "
                                                 "column with blob data type");

        if constexpr (std::is_integral_v<T> && sizeof(T) == 64)
        {
            simdb_assert(dt_ != SqlDataType::int32_t, "Cannot assign 64-bit default value to 32-bit column");
        }

        if constexpr (std::is_integral_v<T>)
        {
            simdb_assert(dt_ != SqlDataType::string_t, "Cannot assign integral default value to string column");
        }

        if constexpr (std::is_same_v<T, const char*>)
        {
            simdb_assert(dt_ != SqlDataType::int32_t&& dt_ != SqlDataType::int64_t,
                         "Cannot assign string default value to an integer column");
        }

        std::ostringstream ss;
        writeDefaultValue_(ss, val);
        setDefaultValueStr_(ss.str());

        simdb_assert(!default_val_string_.empty(), "Unable to convert default value " << val << " into a std::string");
    }

    /// Called in order to set default values for TEXT columns.
    void setDefaultValue(const std::string& val)
    {
        simdb_assert(dt_ == SqlDataType::string_t, "Unable to set default value string (data type mismatch)");
        simdb_assert(!unique_col_, "Cannot set default column value on a unique column");

        setDefaultValueStr_(val);
    }

    /// Check if this column has a default value set or not.
    bool hasDefaultValue() const { return !default_val_string_.empty(); }

    /// Get this Column's default value. These are returned as
    /// strings since the schema creation command is one string,
    /// e.g. "CREATE TABLE ..."
    const std::string& getDefaultValueAsString() const { return default_val_string_; }

    /// Ensure the given column is unique:
    ///   CREATE TABLE my_table (
    ///     Id INTEGER PRIMARY KEY,
    ///     Timestamp INTEGER UNIQUE    <---
    ///   );
    void ensureUnique()
    {
        simdb_assert(!hasDefaultValue(), "Column has default value; cannot make unique");
        unique_col_ = true;
    }

    /// Check if this column should be created with the UNIQUE tag.
    bool isUnique() const { return unique_col_; }

private:
    /// Default values are stringified. For doubles, we need maximum precision.
    void writeDefaultValue_(std::ostringstream& oss, const double val) const
    {
        oss << std::numeric_limits<long double>::digits10 + 1 << val;
    }

    /// Default values are stringified. For non-doubles, e.g. INT and TEXT
    /// types, we use default precision.
    template <typename T>
    typename std::enable_if<std::is_integral_v<T> || std::is_same_v<T, std::string> || std::is_same_v<T, const char*>,
                            void>::type
    writeDefaultValue_(std::ostringstream& oss, const T& val) const
    {
        oss << val;
    }

    /// Throw for columns that do not support default values.
    template <typename T>
    typename std::enable_if<
        !std::is_integral_v<T> && !std::is_same_v<T, std::string> && !std::is_same_v<T, const char*>, void>::type
    writeDefaultValue_(std::ostringstream&, const T&) const
    {
        throw DBException("Only INT/REAL/TEXT columns support default values");
    }

    /// Set the default-value-as-string.
    void setDefaultValueStr_(const std::string& val) { default_val_string_ = val; }

    /// DatabaseManager needs setDefaultValueStr_
    friend class DatabaseManager;

    /// Column name
    std::string name_;

    /// Column data type
    SqlDataType dt_;

    /// Optional default value (stringified)
    std::string default_val_string_;

    /// Should this column be defined as e.g. "Timestamp INTEGER UNIQUE"?
    bool unique_col_ = false;
};

/*!
 * \class Table
 *
 * \brief Table class used for creating SimDB schemas
 */
class Table
{
public:
    /// Construct with a name.
    Table(const std::string& table_name) :
        name_(table_name)
    {
    }

    /// Get the name of this table.
    const std::string& getName() const { return name_; }

    /// Add a column to this table's schema with a name and data type.
    Table& addColumn(const std::string& name, const SqlDataType dt)
    {
        simdb_assert(!hasColumn(name), "Table already has a column named " << name);
        simdb_assert(name != "Id", "Cannot explicitly create the Id column; call Table::setPrimaryKey(\"Id\")");

        columns_.emplace_back(new Column(name, dt));
        columns_by_name_[name] = columns_.back();
        return *this;
    }

    /// Get a column by its name. Throws if not found.
    const Column& getColumn(const std::string& col_name) const
    {
        auto iter = columns_by_name_.find(col_name);
        simdb_assert(iter != columns_by_name_.end(), "No column named " << col_name << " in table " << name_);

        return *(iter->second);
    }

    /// Check if this table has a column with the given name.
    bool hasColumn(const std::string& col_name) const
    {
        return columns_by_name_.find(col_name) != columns_by_name_.end();
    }

    /// Assign a default value for the given column.
    template <typename T> Table& setColumnDefaultValue(const std::string& col_name, const T default_val)
    {
        auto iter = columns_by_name_.find(col_name);
        simdb_assert(iter != columns_by_name_.end(), "No column named " << col_name << " in table " << name_);

        iter->second->setDefaultValue(default_val);
        return *this;
    }

    /// Assign a default value for the given column.
    Table& setColumnDefaultValue(const std::string& col_name, const std::string& default_val)
    {
        auto iter = columns_by_name_.find(col_name);
        simdb_assert(iter != columns_by_name_.end(), "No column named " << col_name << " in table " << name_);

        iter->second->setDefaultValue(default_val);
        return *this;
    }

    /// Ensure the given column is unique:
    ///   CREATE TABLE my_table (
    ///     Id INTEGER PRIMARY KEY,
    ///     Timestamp INTEGER UNIQUE    <---
    ///   );
    Table& ensureUnique(const std::string& col_name)
    {
        auto iter = columns_by_name_.find(col_name);
        simdb_assert(iter != columns_by_name_.end(), "No column named " << col_name << " in table " << name_);

        iter->second->ensureUnique();
        return *this;
    }

    /// Index this table's records on the given column.
    /// CREATE INDEX IndexName ON TableName(ColumnName)
    Table& createIndexOn(const std::string& col_name) { return createCompoundIndexOn({col_name}); }

    /// Index this table's records on the given columns.
    /// CREATE INDEX IndexName ON TableName(ColA,ColB,ColC)
    Table& createCompoundIndexOn(const std::initializer_list<std::string>& col_names)
    {
        return createCompoundIndexOn(std::vector<std::string>(col_names.begin(), col_names.end()));
    }

    /// See std::initializer_list overload
    Table& createCompoundIndexOn(const std::vector<std::string>& col_names)
    {
        for (const auto& col_name : col_names)
        {
            simdb_assert(columns_by_name_.find(col_name) != columns_by_name_.end(),
                         "Column " << col_name << " does not exist in table " << name_);
        }

        std::ostringstream oss;
        oss << "CREATE INDEX " << name_ << "_Index" << index_creation_strs_.size() + 1 << " ON " << name_ << "(";

        size_t idx = 0;
        auto iter = col_names.begin();
        while (iter != col_names.end())
        {
            oss << *iter;
            if (idx != col_names.size() - 1)
            {
                oss << ",";
            }
            ++iter;
            ++idx;
        }
        oss << ")";

        index_creation_strs_.push_back(oss.str());
        return *this;
    }

    /// Get a list of table indexes. Say we created the table like this:
    ///
    ///   using dt = SqlDataType;
    ///   Schema schema;
    ///   auto& tbl = schema.addTable("MyTable");
    ///   tbl.addColumn("Foo", dt::int32_t);
    ///   tbl.addColumn("Bar", dt::int32_t);
    ///   tbl.addColumn("Fiz", dt::int32_t);
    ///   tbl.addColumn("Buz", dt::int32_t);
    ///
    /// Then added these indexes:
    ///
    ///   tbl.createIndexOn("Foo");
    ///   tbl.createIndexOn("Bar");
    ///   tbl.createCompoundIndexOn({"Fiz", "Buz"});
    ///
    /// Calling getTableIndexes() will return:
    ///
    ///   [
    ///     ["Foo"],
    ///     ["Bar"],
    ///     ["Fiz", "Buz"]
    ///   ]
    std::vector<std::vector<std::string>> getTableIndexes() const
    {
        std::vector<std::vector<std::string>> indexes;
        for (const auto& cmd : index_creation_strs_)
        {
            // Command is something like:
            //   CREATE INDEX <index_name> ON <table_name> (Foo,Bar)
            auto lparen = cmd.rfind("(");
            assert(lparen != std::string::npos);

            auto rparen = cmd.rfind(")");
            assert(rparen != std::string::npos);
            assert(rparen > lparen);

            auto idx_cols = cmd.substr(lparen + 1, rparen - lparen - 1);
            indexes.emplace_back();
            boost::split(indexes.back(), idx_cols, boost::is_any_of(","));

            for (auto& s : indexes.back())
            {
                boost::trim(s);
            }
        }
        return indexes;
    }

    /// Set this table's primary key. Defaults to "Id".
    Table& setPrimaryKey(const std::string& pkey_column)
    {
        if (pkey_column.empty())
        {
            return unsetPrimaryKey();
        }

        simdb_assert(hasColumn(pkey_column) || pkey_column == "Id",
                     "Primary key column does not exist: " << pkey_column);
        primary_key_column_ = pkey_column;
        return *this;
    }

    /// Do not use a primary key for this table.
    Table& unsetPrimaryKey()
    {
        primary_key_column_.clear();
        return *this;
    }

    /// Get the primary key column for this table. Defaults to "Id".
    const std::string& getPrimaryKey() const { return primary_key_column_; }

    /// Read-only access to this table's columns.
    const std::vector<std::shared_ptr<Column>>& getColumns() const { return columns_; }

    /// Equivalency check.
    bool operator==(const Table& rhs) const
    {
        if (name_ != rhs.name_)
        {
            return false;
        }

        if (columns_.size() != rhs.columns_.size())
        {
            return false;
        }

        for (size_t idx = 0; idx < columns_.size(); ++idx)
        {
            if (*(columns_[idx]) != *(rhs.columns_[idx]))
            {
                return false;
            }
        }

        if (getTableIndexes() != rhs.getTableIndexes())
        {
            return false;
        }

        if (primary_key_column_ != rhs.primary_key_column_)
        {
            return false;
        }

        return true;
    }

    /// Equivalency check.
    bool operator!=(const Table& rhs) const { return !(*this == rhs); }

private:
    /// Get a column by its name. Throws if not found.
    Column& getColumn_(const std::string& col_name)
    {
        auto iter = columns_by_name_.find(col_name);
        simdb_assert(iter != columns_by_name_.end(), "No column named " << col_name << " in table " << name_);

        return *(iter->second);
    }

    /// Name of this table
    std::string name_;

    /// Columns in this table
    std::vector<std::shared_ptr<Column>> columns_;

    /// Map of columns by their name
    std::unordered_map<std::string, std::shared_ptr<Column>> columns_by_name_;

    /// List of index creation strings that are executed on the database
    /// when the schema is instantiated:
    ///
    ///     CREATE INDEX IndexName ON TableName(ColumnName)
    ///     CREATE INDEX IndexName ON TableName(ColA,ColB,ColC)
    ///      .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .  .
    std::vector<std::string> index_creation_strs_;

    /// Use an auto-incrementing primary key "Id" for this table by default.
    std::string primary_key_column_ = "Id";

    friend class Connection;

    /// DatabaseManager needs getColumn_
    friend class DatabaseManager;
};

/*!
 * \class Schema
 *
 * \brief This class is used to define SimDB schemas.
 */
class Schema
{
public:
    /// \brief  Create a new Table in this Schema with the given name
    ///
    /// \return Reference to the added table
    Table& addTable(const std::string& table_name)
    {
        for (auto& lhs : tables_)
        {
            simdb_assert(lhs.getName() != table_name,
                         "Cannot add table '" + table_name + "' to schema. A table with that name already exists.");
        }

        tables_.emplace_back(table_name);
        return tables_.back();
    }

    /// Combine this schema with the tables from another schema.
    void appendSchema(const Schema& schema)
    {
        for (const auto& table : schema.getTables())
        {
            if (hasTable(table.getName()))
            {
                auto& existing_table = getTable(table.getName());
                simdb_assert(existing_table == table, "Cannot append schema - it has a table "
                                                      "we already have by that name "
                                                          << "(" << table.getName() << ")");
            } else
            {
                tables_.push_back(table);
            }
        }
    }

    /// Read-only access to this schema's tables.
    const std::deque<Table>& getTables() const { return tables_; }

    /// Get a table by its name. Throws if not found.
    const Table& getTable(const std::string& table_name) const
    {
        for (const auto& table : tables_)
        {
            if (table.getName() == table_name)
            {
                return table;
            }
        }

        throw DBException("Table '" + table_name + "' not found in schema");
    }

    /// Before calling getTable(), you can use this method to
    /// check if a table with the given name exists in this schema.
    bool hasTable(const std::string& table_name) const
    {
        for (const auto& table : tables_)
        {
            if (table.getName() == table_name)
            {
                return true;
            }
        }

        return false;
    }

    /// Equivalency check.
    bool operator==(const Schema& rhs) const
    {
        auto getTableNames = [](const Schema& schema) {
            std::unordered_set<std::string> table_names;
            for (const auto& tbl : schema.getTables())
            {
                table_names.insert(tbl.getName());
            }
            return table_names;
        };

        auto my_table_names = getTableNames(*this);
        auto their_table_names = getTableNames(rhs);
        if (my_table_names != their_table_names)
        {
            return false;
        }

        for (const auto& table_name : my_table_names)
        {
            const auto& my_table = getTable(table_name);
            const auto& their_table = getTable(table_name);
            if (my_table != their_table)
            {
                return false;
            }
        }

        return true;
    }

private:
    /// All the tables in this schema, whether added via
    /// addTable() or appendSchema().
    std::deque<Table> tables_;
};

} // namespace simdb
