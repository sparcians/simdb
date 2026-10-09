// <Blob.hpp> -*- C++ -*-

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace simdb {

/*!
 * \struct SqlBlob
 *
 * \brief Blob descriptor used for writing and reading raw bytes to/from the
 *        database. Holds a pointer and byte count; used with SQL_VALUES() and
 *        blob columns (e.g. getPropertyBlob / setPropertyBlob on SqlRecord).
 */
struct SqlBlob
{
    /// Pointer to the raw bytes to store or that were read.
    const void* data_ptr = nullptr;
    /// Number of bytes at \p data_ptr.
    size_t num_bytes = 0;

    /// Construct from a contiguous container (e.g. std::vector<T>).
    /// \tparam T Element type; sizeof(T) * vals.size() gives num_bytes.
    /// \param vals Container whose data() and size() define the blob.
    template <typename T>
    SqlBlob(const std::vector<T>& vals) :
        data_ptr(vals.data()),
        num_bytes(vals.size() * sizeof(T))
    {
    }

    /// Construct from a contiguous array.
    template <typename T, size_t N>
    SqlBlob(const std::array<T, N>& vals) :
        data_ptr(vals.data()),
        num_bytes(N * sizeof(T))
    {
    }

    SqlBlob() = default;
    SqlBlob(const SqlBlob&) = default;
};

/// Write a short debug representation of the blob to \p os (e.g. blob(addr, n=size)).
inline std::ostream& operator<<(std::ostream& os, const SqlBlob& blob)
{
    if (!blob.data_ptr || blob.num_bytes == 0)
    {
        os << "blob(empty)";
        return os;
    }

    os << "blob(" << std::hex << reinterpret_cast<uintptr_t>(blob.data_ptr) << ", n=" << std::dec << blob.num_bytes
       << ")";

    return os;
}

} // namespace simdb
