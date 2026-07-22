// <ValueContainer.hpp> -*- C++ -*-

#pragma once

#include "simdb/schema/Blob.hpp"
#include "simdb/utils/utf16.hpp"

#include <functional>
#include <memory>
#include <sqlite3.h>
#include <type_traits>
#include <vector>

namespace simdb {

/*!
 * \class ValueContainerBase
 *
 * \brief This class is used for flexible varargs to SQL_VALUES(v1,v2,v3)
 *        where the types of v1/v2/v3 can all be different (int/double/blob...)
 */
class ValueContainerBase
{
public:
    virtual ~ValueContainerBase() = default;
    virtual int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const = 0;
};

/*!
 * \class Integral32ValueContainer
 *
 * \brief Binds an int32_t value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class Integral32ValueContainer : public ValueContainerBase
{
public:
    Integral32ValueContainer(int32_t val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override { return sqlite3_bind_int(stmt, col_idx, val_); }

private:
    int32_t val_;
};

/*!
 * \class IntegralU32ValueContainer
 *
 * \brief Binds a uint32_t value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class IntegralU32ValueContainer : public ValueContainerBase
{
public:
    IntegralU32ValueContainer(uint32_t val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_int64(stmt, col_idx, static_cast<sqlite3_int64>(val_));
    }

private:
    uint32_t val_;
};

/*!
 * \class Integral64ValueContainer
 *
 * \brief Binds an int64_t value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class Integral64ValueContainer : public ValueContainerBase
{
public:
    Integral64ValueContainer(int64_t val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override { return sqlite3_bind_int64(stmt, col_idx, val_); }

private:
    int64_t val_;
};

/*!
 * \class IntegralU64ValueContainer
 *
 * \brief Binds a uint64_t value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class IntegralU64ValueContainer : public ValueContainerBase
{
public:
    IntegralU64ValueContainer(uint64_t val) :
        u16_(utils::uint64_to_utf16(val))
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_text16(stmt, col_idx, u16_.data(), 40, 0);
    }

private:
    std::u16string u16_;
};

/*!
 * \class FloatingPointValueContainer
 *
 * \brief Binds a double value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class FloatingPointValueContainer : public ValueContainerBase
{
public:
    FloatingPointValueContainer(double val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_double(stmt, col_idx, val_);
    }

private:
    double val_;
};

/*!
 * \class StringValueContainer
 *
 * \brief Binds a std::string value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class StringValueContainer : public ValueContainerBase
{
public:
    StringValueContainer(const std::string& val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_text(stmt, col_idx, val_.c_str(), -1, 0);
    }

private:
    std::string val_;
};

/*!
 * \class BlobValueContainer
 *
 * \brief Binds a SqlBlob value to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 */
class BlobValueContainer : public ValueContainerBase
{
public:
    BlobValueContainer(const SqlBlob& val) :
        val_(val)
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_blob(stmt, col_idx, val_.data_ptr, (int)val_.num_bytes, 0);
    }

private:
    SqlBlob val_;
};

/*!
 * \class VectorValueContainer
 *
 * \brief Binds a std::vector<T> as a blob to an INSERT prepared statement (SQL_VALUES / PreparedINSERT).
 * \tparam T Element type of the vector; stored as raw bytes in the BLOB column.
 */
template <typename T> class VectorValueContainer : public ValueContainerBase
{
public:
    VectorValueContainer(const std::vector<T>& val) :
        val_(val)
    {
    }

    VectorValueContainer(std::vector<T>&& val) :
        val_(std::move(val))
    {
    }

    int32_t bind(sqlite3_stmt* stmt, int32_t col_idx) const override
    {
        return sqlite3_bind_blob(stmt, col_idx, val_.data(), (int)val_.size() * sizeof(T), 0);
    }

private:
    std::vector<T> val_;
};

using ValueContainerBasePtr = std::shared_ptr<ValueContainerBase>;

// Handles any integral type (int/unsigned, char/short/long/long long, size_t, enums'
// underlying types, etc.) by dispatching on signedness and size rather than requiring
// an exact fixed-width type match. This is important for portability: e.g. on macOS
// size_t is 'unsigned long' which is a distinct type from uint64_t ('unsigned long long'),
// so an exact std::is_same match against uint64_t would fail there even though both are
// 64-bit. Note bool is excluded and handled by its own overload below.
template <typename T>
inline typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, ValueContainerBasePtr>::type
createValueContainer(T val)
{
    if constexpr (std::is_signed<T>::value)
    {
        if constexpr (sizeof(T) <= sizeof(int32_t))
        {
            return ValueContainerBasePtr(new Integral32ValueContainer(static_cast<int32_t>(val)));
        } else
        {
            return ValueContainerBasePtr(new Integral64ValueContainer(static_cast<int64_t>(val)));
        }
    } else
    {
        if constexpr (sizeof(T) <= sizeof(uint32_t))
        {
            return ValueContainerBasePtr(new IntegralU32ValueContainer(static_cast<uint32_t>(val)));
        } else
        {
            return ValueContainerBasePtr(new IntegralU64ValueContainer(static_cast<uint64_t>(val)));
        }
    }
}

template <typename T>
inline typename std::enable_if<std::is_same<T, bool>::value, ValueContainerBasePtr>::type createValueContainer(T val)
{
    return ValueContainerBasePtr(new Integral32ValueContainer(val ? 1 : 0));
}

template <typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, ValueContainerBasePtr>::type
createValueContainer(T val)
{
    return ValueContainerBasePtr(new FloatingPointValueContainer(val));
}

template <typename T>
inline
    typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value, ValueContainerBasePtr>::type
    createValueContainer(T val)
{
    return ValueContainerBasePtr(new StringValueContainer(val));
}

template <typename T>
inline typename std::enable_if<std::is_same<T, std::string>::value, ValueContainerBasePtr>::type
createValueContainer(const T& val)
{
    return ValueContainerBasePtr(new StringValueContainer(val));
}

template <typename T>
inline typename std::enable_if<std::is_same<T, SqlBlob>::value, ValueContainerBasePtr>::type
createValueContainer(const T& val)
{
    return ValueContainerBasePtr(new BlobValueContainer(val));
}

template <typename T> inline ValueContainerBasePtr createValueContainer(const std::vector<T>& val)
{
    return ValueContainerBasePtr(new VectorValueContainer<T>(val));
}

template <typename T> inline ValueContainerBasePtr createValueContainer(std::vector<T>&& val)
{
    return ValueContainerBasePtr(new VectorValueContainer<T>(std::move(val)));
}

template <typename T>
inline typename std::enable_if<std::is_same<T, ValueContainerBasePtr>::value, ValueContainerBasePtr>::type
createValueContainer(T val)
{
    return val;
}

enum class ValueReaderTypes { BACKPOINTER, FUNCPOINTER };

/*!
 * \class ScalarValueReader
 *
 * \brief Helper class to store either backpointers or function pointers
 *        in the same vector / data structure. Used for reading values
 *        from objects' member variables or getter functions.
 */
template <typename T> class ScalarValueReader
{
public:
    typedef struct
    {
        ValueReaderTypes getter_type;
        const T* backpointer;
        std::function<T()> funcpointer;
    } ValueReader;

    /// Construct with a backpointer to the data value.
    ScalarValueReader(const T* data_ptr)
    {
        reader_.backpointer = data_ptr;
        reader_.getter_type = ValueReaderTypes::BACKPOINTER;

        static_assert(std::is_integral<T>::value || std::is_floating_point<T>::value,
                      "ScalarValueReader only supports integral and "
                      "floating-point types!");
    }

    /// Construct with a function pointer to get the data.
    ScalarValueReader(std::function<T()> func_ptr)
    {
        reader_.funcpointer = func_ptr;
        reader_.getter_type = ValueReaderTypes::FUNCPOINTER;

        static_assert(std::is_integral<T>::value || std::is_floating_point<T>::value,
                      "ScalarValueReader only supports integral and "
                      "floating-point types!");
    }

    /// Read the data value.
    T getValue() const
    {
        if (reader_.getter_type == ValueReaderTypes::BACKPOINTER)
        {
            return *reader_.backpointer;
        } else
        {
            return reader_.funcpointer();
        }
    }

private:
    ValueReader reader_;
};

} // namespace simdb
