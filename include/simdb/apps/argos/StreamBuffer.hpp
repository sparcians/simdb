// <StreamBuffer.hpp> -*- C++ -*-

#pragma once

#include "simdb/utils/SafeWeakPtr.hpp"
#include "simdb/utils/TinyStrings.hpp"
#include "simdb/utils/TypeTraits.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simdb::argos {

/// \class StreamBuffer
/// \brief Utility class which wraps a char buffer with typed append operators.
class StreamBuffer
{
public:
    StreamBuffer(std::vector<char>& out, std::optional<safe_weak_ptr<TinyStrings<>>> tiny_strings = std::nullopt,
                 bool clear_first = true) :
        out_(out),
        tiny_strings_(std::move(tiny_strings))
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

    void append(const std::string& s) { append(tiny_strings_.value()->getStringID(s)); }

    template <typename T>
    std::enable_if_t<std::is_trivial_v<T> && std::is_standard_layout_v<T> && !std::is_enum_v<T>, void>
    append(const T& val)
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

    template <typename T> std::enable_if_t<std::is_enum_v<T>, void> append(const T val)
    {
        using underlying_t = std::underlying_type_t<T>;
        append(static_cast<underlying_t>(val));
    }

    size_t size() const { return out_.size(); }

    bool operator==(const StreamBuffer& other) const { return out_ == other.out_; }

    bool operator==(const std::vector<char>& other) const { return out_ == other; }

    bool usesExpiredTinyStrings(const safe_weak_ptr<TinyStrings<>>& current) const
    {
        if (!tiny_strings_.has_value())
        {
            return false;
        }
        if (tiny_strings_->expired())
        {
            return true;
        }
        const auto stored = tiny_strings_->try_lock();
        const auto live = current.try_lock();
        return !stored || !live || stored.get() != live.get();
    }

private:
    std::vector<char>& out_;
    std::optional<safe_weak_ptr<TinyStrings<>>> tiny_strings_;
};

} // namespace simdb::argos
