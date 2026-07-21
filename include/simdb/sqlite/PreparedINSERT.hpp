#pragma once

#include "simdb/Assert.hpp"
#include "simdb/schema/Blob.hpp"
#include "simdb/schema/SchemaDef.hpp"
#include "simdb/sqlite/Connection.hpp"
#include "simdb/sqlite/Transaction.hpp"
#include "simdb/utils/ValidValue.hpp"
#include "simdb/utils/utf16.hpp"

namespace simdb {

/*!
 * \class PreparedINSERT
 *
 * \brief Reusable prepared INSERT for high-volume table inserts. Obtained from
 *        DatabaseManager::prepareINSERT(); use setColumnValue() for each
 *        column (by 0-based index) then createRecord() to insert a row.
 *        Column types must match the table schema.
 */
class PreparedINSERT
{
public:
    /// \brief Internal constructor; use DatabaseManager::prepareINSERT() to obtain instances.
    PreparedINSERT(SQLitePreparedStatement&& stmt, const std::vector<SqlDataType>& col_dtypes,
                   std::shared_ptr<Connection> db_conn) :
        prepared_stmt_(std::move(stmt)),
        stmt_(prepared_stmt_),
        col_dtypes_(col_dtypes),
        db_conn_(db_conn)
    {
    }

    /// \brief Set the value for the given column index (int32_t).
    /// \param col_idx 0-based column index.
    /// \param ival Value to set for the column.
    void setColumnValue(uint32_t col_idx, const int32_t ival)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::int32_t, "Column " << col_idx + 1 << " is not an int32_t");

        sqlite3_bind_int(stmt_, (int32_t)col_idx + 1, ival);
    }

    /// \brief Set the value for the given column index (uint32_t).
    /// \param col_idx 0-based column index.
    /// \param ival Value to set for the column.
    void setColumnValue(uint32_t col_idx, const uint32_t ival)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::uint32_t,
                     "Column " << col_idx + 1 << " is not a uint32_t");

        sqlite3_bind_int64(stmt_, (int32_t)col_idx + 1, static_cast<sqlite3_int64>(ival));
    }

    /// \brief Set the value for the given column index (int64_t).
    /// \param col_idx 0-based column index.
    /// \param ival Value to set for the column.
    void setColumnValue(uint32_t col_idx, const int64_t ival)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::int64_t, "Column " << col_idx + 1 << " is not an int64_t");

        sqlite3_bind_int64(stmt_, (int32_t)col_idx + 1, ival);
    }

    /// \brief Set the value for the given column index (uint64_t).
    /// \param col_idx 0-based column index.
    /// \param ival Value to set for the column.
    void setColumnValue(uint32_t col_idx, const uint64_t ival)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::uint64_t,
                     "Column " << col_idx + 1 << " is not a uint64_t");

        auto u16 = utils::uint64_to_utf16(ival);
        sqlite3_bind_text16(stmt_, (int32_t)col_idx + 1, u16.data(), 40, SQLITE_TRANSIENT);
    }

    /// \brief Set the value for the given column index (double/float).
    /// \tparam ColumnT double or float
    /// \param col_idx 0-based column index.
    /// \param dval Value to set for the column.
    template <typename ColumnT>
    typename std::enable_if<std::is_floating_point_v<ColumnT>, void>::type setColumnValue(uint32_t col_idx,
                                                                                          const ColumnT dval)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::double_t, "Column " << col_idx + 1 << " is not a double");

        sqlite3_bind_double(stmt_, (int32_t)col_idx + 1, dval);
    }

    /// \brief Set the value for the given column index (blob from std::vector<T>).
    /// \tparam T Blob element type.
    /// \param col_idx 0-based column index.
    /// \param blobval Value to set for the column.
    template <typename T> void setColumnValue(uint32_t col_idx, const std::vector<T>& blobval)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::blob_t, "Column " << col_idx + 1 << " is not a BLOB");

        sqlite3_bind_blob(stmt_, (int32_t)col_idx + 1, blobval.data(), blobval.size() * sizeof(T), 0);
    }

    /// \brief Set the value for the given column index (SqlBlob).
    /// \param col_idx 0-based column index.
    /// \param blobval Value to set for the column.
    void setColumnValue(uint32_t col_idx, const SqlBlob& blobval)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::blob_t, "Column " << col_idx << " is not a BLOB");

        sqlite3_bind_blob(stmt_, (int32_t)col_idx + 1, blobval.data_ptr, (int32_t)blobval.num_bytes, 0);
    }

    /// \brief Set the value for the given column index (std::string).
    /// \param col_idx 0-based column index.
    /// \param strval Value to set for the column.
    void setColumnValue(uint32_t col_idx, const std::string& strval)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::string_t, "Column " << col_idx << " is not a TEXT");

        sqlite3_bind_text(stmt_, (int32_t)col_idx + 1, strval.c_str(), -1, SQLITE_TRANSIENT);
    }

    /// \brief Set the value for the given column index (const char*).
    /// \param col_idx 0-based column index.
    /// \param strval Value to set for the column.
    void setColumnValue(uint32_t col_idx, const char* strval)
    {
        simdb_assert(col_dtypes_.at(col_idx) == SqlDataType::string_t, "Column " << col_idx << " is not a TEXT");

        sqlite3_bind_text(stmt_, (int32_t)col_idx + 1, strval, -1, SQLITE_STATIC);
    }

    /// \brief Execute the prepared INSERT and return the new row ID.
    /// \param clear_bindings If true, clears all bound values after the INSERT.
    ///        If false, bound values are reused for the next createRecord();
    ///        ensure they remain valid (e.g. not out of scope).
    /// \return The row ID of the newly inserted record.
    int createRecord(bool clear_bindings = true)
    {
        auto rc = SQLiteReturnCode(sqlite3_step(stmt_));
        simdb_assert(rc == SQLITE_DONE, "Could not perform INSERT. Error: " << sqlite3_errmsg(db_conn_->getDatabase()));

        sqlite3_reset(stmt_);
        if (clear_bindings)
        {
            sqlite3_clear_bindings(stmt_);
        }
        return db_conn_->getLastInsertRowId();
    }

    /// \brief Instead of calling setColumnValue() multiple times followed
    /// by createRecord(), you can just call this method once.
    /// \tparam ColumnT The type of the first column.
    /// \tparam RestOfColumns The types of the remaining columns.
    /// \param col_val The value for the first column.
    /// \param rest The values for the remaining columns.
    /// \note The variadic template arguments must match ALL columns in this
    /// table, both in number and in data type.
    /// \return The row ID of the newly inserted record.
    template <typename ColumnT, typename... RestOfColumns>
    int createRecordWithColValues(const ColumnT& col_val, RestOfColumns&&... rest)
    {
        if (!one_shot_col_idx_.isValid())
        {
            one_shot_col_idx_ = 0;
        }
        setColumnValue(one_shot_col_idx_, col_val);
        ++one_shot_col_idx_.getValue();
        return createRecordWithColValues(std::forward<RestOfColumns>(rest)...);
    }

    /// \brief Base case for variadic createRecordWithColValues().
    /// \tparam ColumnT The type of the column.
    /// \param col_val The value for the column.
    /// \return The row ID of the newly inserted object.
    template <typename ColumnT> int createRecordWithColValues(const ColumnT& col_val)
    {
        if (!one_shot_col_idx_.isValid())
        {
            one_shot_col_idx_ = 0;
        }
        setColumnValue(one_shot_col_idx_, col_val);
        auto id = createRecord();
        one_shot_col_idx_.clearValid();
        return id;
    }

private:
    SQLitePreparedStatement prepared_stmt_;
    sqlite3_stmt* stmt_ = nullptr;
    std::vector<SqlDataType> col_dtypes_;
    std::shared_ptr<Connection> db_conn_;
    ValidValue<uint32_t> one_shot_col_idx_;
};

} // namespace simdb
