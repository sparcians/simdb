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

/// \class StreamBuffer
/// \brief Utility class which wraps a char buffer with ostream operators.
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
    buf.append(&underlying, sizeof(U), trace_label);
}

inline StreamBuffer& operator<<(StreamBuffer& buf, const std::string& val)
{
    buf.append(val.data(), val.size());
    return buf;
}

inline StreamBuffer& operator<<(StreamBuffer& buf, const char* val)
{
    if (val != nullptr)
    {
        buf.append(val, std::strlen(val));
    }
    return buf;
}

inline StreamBuffer& operator<<(StreamBuffer& buf, const bool val)
{
    using bool_type = typename StreamBuffer::bool_type;
    const bool_type byte = val ? bool_type{1} : bool_type{0};
    buf.append(&byte, sizeof(byte));
    return buf;
}

template <typename T, typename Alloc>
inline StreamBuffer& operator<<(StreamBuffer& buf, const std::vector<T, Alloc>& val)
{
    if constexpr (std::is_trivial_v<T> && std::is_standard_layout_v<T>)
    {
        buf.append(val.data(), val.size() * sizeof(T));
    }
    else
    {
        for (const auto& v : val)
        {
            buf << v;
        }
    }
    return buf;
}

template <typename T>
constexpr bool is_stream_array_value_type_v =
    (std::is_scalar_v<T> && std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && !std::is_enum_v<T> &&
     !std::is_same_v<T, bool>) ||
    std::is_enum_v<T> || std::is_same_v<T, bool>;

template <typename T, size_t N> StreamBuffer& operator<<(StreamBuffer& buf, const T (&val)[N])
{
    using ValueType = std::remove_cv_t<T>;
    static_assert(is_stream_array_value_type_v<ValueType>,
                  "StreamBuffer array operator<< requires scalar POD, scalar enum, or scalar bool value types");

    for (const auto& elem : val)
    {
        buf << elem;
    }
    return buf;
}

template <typename T, size_t N> StreamBuffer& operator<<(StreamBuffer& buf, const std::array<T, N>& val)
{
    using ValueType = std::remove_cv_t<T>;
    static_assert(is_stream_array_value_type_v<ValueType>,
                  "StreamBuffer std::array operator<< requires scalar POD, scalar enum, or scalar bool value types");

    for (const auto& elem : val)
    {
        buf << elem;
    }
    return buf;
}

template <typename T, typename ValueType = std::remove_cv_t<std::remove_reference_t<T>>,
          std::enable_if_t<!std::is_enum_v<ValueType> && !std::is_same_v<ValueType, bool>, int> = 0>
StreamBuffer& operator<<(StreamBuffer& buf, const T& val)
{
    static_assert(std::is_trivial_v<ValueType> && std::is_standard_layout_v<ValueType>,
                  "StreamBuffer::operator<< requires memcpy-able data");
    buf.append(&val, sizeof(T));
    return buf;
}

template <typename T, typename ValueType = std::remove_cv_t<std::remove_reference_t<T>>,
          std::enable_if_t<std::is_enum_v<ValueType>, int> = 0>
StreamBuffer& operator<<(StreamBuffer& buf, const T& val)
{
    using Underlying = std::underlying_type_t<ValueType>;
    const auto underlying = static_cast<Underlying>(val);
    buf << underlying;
    return buf;
}

} // namespace simdb
