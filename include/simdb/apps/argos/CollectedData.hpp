// <CollectedData.hpp> -*- C++ -*-

#pragma once

#include "simdb/utils/StreamBuffer.hpp"

#include <string>

namespace simdb::argos {

/// \class CollectedData
/// \brief Wrapper around StreamBuffer which provides collection-specific APIs
class CollectedData
{
public:
    CollectedData(uint16_t cid, TinyStrings<>* tiny_strings) :
        cid_(cid),
        buffer_(data_, tiny_strings)
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

private:
    uint16_t cid_ = 0;
    std::vector<char> data_;
    StreamBuffer buffer_;
};

} // namespace simdb::argos
