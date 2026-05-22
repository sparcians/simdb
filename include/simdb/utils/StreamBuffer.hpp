// <StreamBuffer.hpp> -*- C++ -*-

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simdb {

/// \class StreamBuffer
/// \brief Utility class which wraps a char buffer with typed append operators.
class StreamBuffer
{
public:
    StreamBuffer(std::vector<char>& out, bool clear_first = true) :
        out_(out)
    {
        if (clear_first)
        {
            out_.clear();
        }
    }

    void append(const void* data, const size_t num_bytes)
    {
        auto bytes = static_cast<const char*>(data);
        out_.insert(out_.end(), bytes, bytes + num_bytes);
    }

    void append(const bool val) { append(static_cast<uint8_t>(val)); }

    template <typename T> void append(const T& val)
    {
        static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
        append(&val, sizeof(T));
    }

    template <typename T, typename Alloc> void append(const std::vector<T, Alloc>& val)
    {
        static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
        append(val.data(), val.size() * sizeof(T));
    }

    template <typename T, size_t N> void append(const T (&val)[N])
    {
        static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
        append(val, N * sizeof(T));
    }

    template <typename T, size_t N> void append(const std::array<T, N>& val)
    {
        static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
        append(val.data(), N * sizeof(T));
    }

    template <typename T> std::enable_if_t<std::is_enum_v<T>, void> append(const T val) = delete;

    size_t size() const { return out_.size(); }

    bool operator==(const StreamBuffer& other) const { return out_ == other.out_; }

    bool operator==(const std::vector<char>& other) const { return out_ == other; }

private:
    std::vector<char>& out_;
};

} // namespace simdb
