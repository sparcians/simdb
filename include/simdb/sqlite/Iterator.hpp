// <Iterator.hpp> -*- C++ -*-

#pragma once

#include "simdb/sqlite/Transaction.hpp"
#include "simdb/utils/utf16.hpp"

#include <memory>
#include <optional>
#include <sqlite3.h>
#include <string.h>
#include <string>
#include <type_traits>
#include <vector>

namespace simdb {

namespace detail {
template <typename T> struct unrolled_type
{
    using type = T;
};

template <typename T> struct unrolled_type<std::optional<T>>
{
    using type = T;
};

template <typename T> using unrolled_type_t = typename unrolled_type<T>::type;

template <typename T1, typename T2>
struct unrolled_is_same
    : std::conditional_t<std::is_same_v<unrolled_type_t<T1>, unrolled_type_t<T2>>, std::true_type, std::false_type>
{
};

template <typename T1, typename T2> inline constexpr bool unrolled_is_same_v = unrolled_is_same<T1, T2>::value;

template <typename T> struct is_optional : std::false_type
{
};

template <typename T> struct is_optional<std::optional<T>> : std::true_type
{
};

template <typename T> inline constexpr bool is_optional_v = is_optional<T>::value;

inline bool columnIsNull(sqlite3_stmt* stmt, const int idx)
{
    return sqlite3_column_type(stmt, idx) == SQLITE_NULL;
}

inline void requireNonNullColumn(sqlite3_stmt* stmt, const int idx, const std::string& col_name)
{
    if (columnIsNull(stmt, idx))
    {
        throw DBException("Column '") << col_name << "' is NULL; use std::optional overload";
    }
}
} // namespace detail

/*!
 * \class ResultWriterBase
 *
 * \brief Base class for all SELECT objects that are responsible for
 *        writing record values to the user's local variables whenever
 *        a query's result set iterator is advanced.
 */
class ResultWriterBase
{
public:
    /// Destroy
    virtual ~ResultWriterBase() = default;

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    virtual void writeToUserVar(sqlite3_stmt* stmt, const int idx) const = 0;

    /// Return a new copy of this writer.
    virtual ResultWriterBase* clone() const = 0;

    /// \brief Return the column name for this writer.
    ///
    /// Used when creating the query's prepared statement:
    /// SELECT ColA,ColB FROM Table WHERE Id=44 AND Name='foo'
    const std::string& getColName() const { return col_name_; }

protected:
    ResultWriterBase(const char* col_name) :
        col_name_(col_name)
    {
    }

private:
    std::string col_name_;
};

/*!
 * \class ResultWriterInt32
 *
 * \brief Responsible for writing int32 record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename IntT = int32_t> class ResultWriterInt32 : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<IntT, int32_t>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterInt32(const char* col_name, IntT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<IntT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else
        {
            detail::requireNonNullColumn(stmt, idx, getColName());
        }

        *user_var_ = sqlite3_column_int(stmt, idx);
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override { return new ResultWriterInt32<IntT>(getColName().c_str(), user_var_); }

private:
    IntT* user_var_;
};

/*!
 * \class ResultWriterUInt32
 *
 * \brief Responsible for writing uint32 record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename UIntT = uint32_t> class ResultWriterUInt32 : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<UIntT, uint32_t>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterUInt32(const char* col_name, UIntT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<UIntT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else
        {
            detail::requireNonNullColumn(stmt, idx, getColName());
        }

        sqlite3_int64 tmp = sqlite3_column_int64(stmt, idx);

        if (tmp < 0 || tmp > UINT32_MAX)
        {
            throw DBException("Value out of range for uint32_t");
        }

        *user_var_ = static_cast<uint32_t>(tmp);
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override { return new ResultWriterUInt32<UIntT>(getColName().c_str(), user_var_); }

private:
    UIntT* user_var_;
};

/*!
 * \class ResultWriterInt64
 *
 * \brief Responsible for writing int64 record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename IntT = int64_t> class ResultWriterInt64 : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<IntT, int64_t>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterInt64(const char* col_name, IntT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<IntT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else
        {
            detail::requireNonNullColumn(stmt, idx, getColName());
        }

        *user_var_ = sqlite3_column_int64(stmt, idx);
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override { return new ResultWriterInt64<IntT>(getColName().c_str(), user_var_); }

private:
    IntT* user_var_;
};

/*!
 * \class ResultWriterUInt64
 *
 * \brief Responsible for writing uint64 record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename UIntT = uint64_t> class ResultWriterUInt64 : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<UIntT, uint64_t>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterUInt64(const char* col_name, UIntT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<UIntT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else
        {
            detail::requireNonNullColumn(stmt, idx, getColName());
        }

        const void* blob = sqlite3_column_text16(stmt, idx);
        if (sqlite3_column_type(stmt, idx) == SQLITE_TEXT && blob != nullptr)
        {
            const char16_t* utf16str = static_cast<const char16_t*>(blob);
            *user_var_ = utils::utf16_to_uint64(utf16str, 20);
        } else
        {
            throw DBException("Invalid data / data type");
        }
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override { return new ResultWriterUInt64<UIntT>(getColName().c_str(), user_var_); }

private:
    UIntT* user_var_;
};

/*!
 * \class ResultWriterDouble
 *
 * \brief Responsible for writing double record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename DoubleT = double> class ResultWriterDouble : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<DoubleT, double>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterDouble(const char* col_name, DoubleT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<DoubleT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else
        {
            detail::requireNonNullColumn(stmt, idx, getColName());
        }

        *user_var_ = sqlite3_column_double(stmt, idx);
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override
    {
        return new ResultWriterDouble<DoubleT>(getColName().c_str(), user_var_);
    }

private:
    DoubleT* user_var_;
};

/*!
 * \class ResultWriterString
 *
 * \brief Responsible for writing text record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename StringT = std::string> class ResultWriterString : public ResultWriterBase
{
    static_assert(detail::unrolled_is_same_v<StringT, std::string>);

public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterString(const char* col_name, StringT* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        if constexpr (detail::is_optional_v<StringT>)
        {
            if (detail::columnIsNull(stmt, idx))
            {
                *user_var_ = std::nullopt;
                return;
            }
        } else if (detail::columnIsNull(stmt, idx))
        {
            *user_var_ = "";
            return;
        }

        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
        *user_var_ = text ? text : "";
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override
    {
        return new ResultWriterString<StringT>(getColName().c_str(), user_var_);
    }

private:
    StringT* user_var_;
};

/*!
 * \class ResultWriterBlob<T>
 *
 * \brief Responsible for writing blob record values to the user's local
 *        variables whenever a query's result set iterator is advanced.
 */
template <typename T> class ResultWriterBlob : public ResultWriterBase
{
public:
    /// \brief Construction
    /// \param col_name Name of the selected column
    /// \param user_var Pointer to the local variable where result values are
    /// written to
    ResultWriterBlob(const char* col_name, std::vector<T>* user_var) :
        ResultWriterBase(col_name),
        user_var_(user_var)
    {
    }

    /// Read the value for the prepared statement at the given column index
    /// and copy it to the user's local variable.
    void writeToUserVar(sqlite3_stmt* stmt, const int idx) const override
    {
        const void* data = sqlite3_column_blob(stmt, idx);
        const int bytes = sqlite3_column_bytes(stmt, idx);
        user_var_->resize(bytes / sizeof(T));
        memcpy(user_var_->data(), data, bytes);
    }

    /// Return a new copy of this writer.
    ResultWriterBase* clone() const override { return new ResultWriterBlob(getColName().c_str(), user_var_); }

private:
    std::vector<T>* user_var_;
};

/*!
 * \class SqlResultIterator
 *
 * \brief This class is returned by SqlQuery::getResultSet() and
 *        is used to iterate over a query result set.
 */
class SqlResultIterator
{
public:
    /// Construct with a prepared statement and the result writers that read
    /// column values and write them into the user's local variables.
    SqlResultIterator(sqlite3_stmt* stmt, std::vector<std::shared_ptr<ResultWriterBase>>&& result_writers) :
        stmt_(stmt),
        result_writers_(std::move(result_writers))
    {
    }

    /// Finalize the prepared statement on destruction.
    ~SqlResultIterator()
    {
        if (stmt_)
        {
            sqlite3_finalize(stmt_);
        }
    }

    /// Get the next record, populate the user's local variables,
    /// and return TRUE if the record was found. FALSE is returned
    /// when the entire result set has been iterated over.
    bool getNextRecord()
    {
        auto rc = SQLiteReturnCode(sqlite3_step(stmt_));
        if (rc == SQLITE_ROW)
        {
            for (size_t idx = 0; idx < result_writers_.size(); ++idx)
            {
                result_writers_[idx]->writeToUserVar(stmt_, (int)idx);
            }
            return true;
        } else if (rc != SQLITE_DONE)
        {
            throw DBException(sqlite3_errmsg(sqlite3_db_handle(stmt_)));
        }

        return false;
    }

    /// Go back to the beginning of the result set if you need
    /// to iterate over it again.
    void reset()
    {
        if (SQLiteReturnCode(sqlite3_reset(stmt_)))
        {
            throw DBException(sqlite3_errmsg(sqlite3_db_handle(stmt_)));
        }
    }

private:
    /// Prepared statement
    sqlite3_stmt* const stmt_;

    /// Writers that read column values and write them into the user's local
    /// variables
    std::vector<std::shared_ptr<ResultWriterBase>> result_writers_;
};

} // namespace simdb
