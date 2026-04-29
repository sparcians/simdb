// <StreamBuffer.hpp> -*- C++ -*-

#pragma once

#include "simdb/utils/CollectionByteTrace.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simdb {

template <typename T>
constexpr bool is_stream_array_value_type_v =
    (std::is_scalar_v<T> && std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && !std::is_enum_v<T> &&
     !std::is_same_v<T, bool>) ||
     std::is_enum_v<T> || std::is_same_v<T, bool>;

/// \class StreamBuffer
/// \brief Utility class which wraps a char buffer with typed append operators.
class StreamBuffer
{
public:
    /// \param participate_in_byte_trace When false, this buffer never records to
    ///        \ref simdb::utils::active_collection_byte_tracer (use for scratch
    ///        buffers whose bytes are not part of the persisted collection layout).
    StreamBuffer(std::vector<char>& out, bool clear_first = true, bool participate_in_byte_trace = true) :
        out_(out), participate_in_byte_trace_(participate_in_byte_trace)
    {
        if (clear_first)
        {
            out_.clear();
        }
    }

    void append(const void* data, const size_t num_bytes, const std::string_view trace_description = {})
    {
        if (participate_in_byte_trace_)
        {
            if (auto* tracer = utils::active_collection_byte_tracer())
            {
                tracer->recordWrite(num_bytes, trace_description);
            }
        }
        auto bytes = static_cast<const char*>(data);
        out_.insert(out_.end(), bytes, bytes + num_bytes);
    }

    /// Append raw bytes without notifying the byte tracer (caller already logged them).
    void append_without_byte_trace(const void* data, const size_t num_bytes)
    {
        auto bytes = static_cast<const char*>(data);
        out_.insert(out_.end(), bytes, bytes + num_bytes);
    }

    void appendValue(
        const void* data,
        const size_t num_bytes,
        const std::string_view trace_description,
        const std::string_view value_repr)
    {
        if (participate_in_byte_trace_)
        {
            if (auto* tracer = utils::active_collection_byte_tracer())
            {
                tracer->recordValueWrite(num_bytes, trace_description, value_repr);
            }
        }
        auto bytes = static_cast<const char*>(data);
        out_.insert(out_.end(), bytes, bytes + num_bytes);
    }

    void append(const std::string& val, const std::string_view trace_description = {})
    {
        append(val.data(), val.size(), trace_description);
    }

    void append(const char* val, const std::string_view trace_description = {})
    {
        if (val != nullptr)
        {
            append(val, std::strlen(val), trace_description);
        }
    }

    void append(const bool val, const std::string_view trace_description = {})
    {
        using bool_type = typename StreamBuffer::bool_type;
        const bool_type byte = val ? bool_type{1} : bool_type{0};
        append(&byte, sizeof(byte), trace_description);
    }

    template <typename T, typename Alloc>
    void append(const std::vector<T, Alloc>& val, const std::string_view trace_description = {})
    {
        if constexpr (std::is_trivial_v<T> && std::is_standard_layout_v<T>)
        {
            append(val.data(), val.size() * sizeof(T), trace_description);
        }
        else
        {
            for (const auto& v : val)
            {
                append(v, trace_description);
            }
        }
    }

    template <typename T, size_t N>
    void append(const T (&val)[N], const std::string_view trace_description = {})
    {
        using ValueType = std::remove_cv_t<T>;
        static_assert(is_stream_array_value_type_v<ValueType>,
                      "StreamBuffer::append(array) requires scalar POD, scalar enum, or scalar bool value types");

        for (const auto& elem : val)
        {
            append(elem, trace_description);
        }
    }

    template <typename T, size_t N>
    void append(const std::array<T, N>& val, const std::string_view trace_description = {})
    {
        using ValueType = std::remove_cv_t<T>;
        static_assert(is_stream_array_value_type_v<ValueType>,
                      "StreamBuffer::append(std::array) requires scalar POD, scalar enum, or scalar bool value types");

        for (const auto& elem : val)
        {
            append(elem, trace_description);
        }
    }

    template <typename T, typename ValueType = std::remove_cv_t<std::remove_reference_t<T>>,
              std::enable_if_t<!std::is_enum_v<ValueType> && !std::is_same_v<ValueType, bool>, int> = 0>
    void append(const T& val, const std::string_view trace_description = {})
    {
        static_assert(std::is_trivial_v<ValueType> && std::is_standard_layout_v<ValueType>,
                      "StreamBuffer::append requires memcpy-able data");
        append(&val, sizeof(T), trace_description);
    }

    template <typename T, typename ValueType = std::remove_cv_t<std::remove_reference_t<T>>,
              std::enable_if_t<!std::is_enum_v<ValueType> && !std::is_same_v<ValueType, bool>, int> = 0>
    void appendValue(
        const T& val,
        const std::string_view trace_description,
        const std::string_view value_repr)
    {
        static_assert(std::is_trivial_v<ValueType> && std::is_standard_layout_v<ValueType>,
                      "StreamBuffer::appendValue requires memcpy-able data");
        appendValue(&val, sizeof(T), trace_description, value_repr);
    }

    template <typename T, typename ValueType = std::remove_cv_t<std::remove_reference_t<T>>,
              std::enable_if_t<std::is_enum_v<ValueType>, int> = 0>
    void append(const T& val, const std::string_view trace_description = {})
    {
        using Underlying = std::underlying_type_t<ValueType>;
        const auto underlying = static_cast<Underlying>(val);
        append(underlying, trace_description);
    }

    size_t size() const { return out_.size(); }

    /// Read-only view of bytes written so far (e.g. for post-append trace introspection).
    const std::vector<char>& byte_storage() const { return out_; }

    using bool_type = uint8_t;

    bool operator==(const StreamBuffer& other) const { return out_ == other.out_; }

    bool operator==(const std::vector<char>& other) const { return out_ == other; }

private:
    std::vector<char>& out_;
    bool participate_in_byte_trace_ = true;
};

/// Serialize an enum as its fixed-width underlying integer, with an optional byte-trace label.
template <typename EnumT>
inline void append_traced_enum(StreamBuffer& buf, const EnumT value, const char* trace_label)
{
    using U = std::underlying_type_t<EnumT>;
    const U underlying = static_cast<U>(value);
    buf.append(underlying, trace_label);
}

} // namespace simdb
