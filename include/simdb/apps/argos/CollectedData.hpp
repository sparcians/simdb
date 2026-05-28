// <CollectedData.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/StreamBuffer.hpp"

#include <optional>
#include <string>

namespace simdb::argos {

/// \class CollectedData
/// \brief Wrapper around StreamBuffer which provides collection-specific APIs
class CollectedData
{
public:
    CollectedData(uint16_t cid) :
        CollectedData(cid, std::nullopt)
    {
    }

    CollectedData(uint16_t cid, safe_weak_ptr<TinyStrings<>> tiny_strings) :
        CollectedData(cid, std::make_optional(std::move(tiny_strings)))
    {
    }

    CollectedData(uint16_t cid, std::optional<safe_weak_ptr<TinyStrings<>>> tiny_strings) :
        cid_(cid),
        tiny_strings_(std::move(tiny_strings)),
        buffer_(data_, tiny_strings_)
    {
        reset();
    }

    CollectedData(CollectedData&&) = delete;
    CollectedData(const CollectedData&) = default;

    uint16_t getCID() const { return cid_; }

    const std::vector<char>& getData() const { return data_; }

    StreamBuffer& getBuffer() { return buffer_; }

    void reset()
    {
        data_.clear();
        buffer_.append(cid_);
    }

    bool usesExpiredTinyStrings(const safe_weak_ptr<TinyStrings<>>& current) const
    {
        return buffer_.usesExpiredTinyStrings(current);
    }

private:
    uint16_t cid_ = 0;
    std::optional<safe_weak_ptr<TinyStrings<>>> tiny_strings_;
    std::vector<char> data_;
    StreamBuffer buffer_;
};

} // namespace simdb::argos
