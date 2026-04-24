// <CollectedData.hpp> -*- C++ -*-

#pragma once

#include "simdb/utils/StreamBuffer.hpp"

namespace simdb::collection {

/// \class CollectedData
/// \brief Wrapper around StreamBuffer which provides collection-specific APIs
class CollectedData
{
public:
    explicit CollectedData(uint16_t cid)
        : cid_(cid)
    {
        buffer_.append(&cid_, sizeof(cid_), "cid");
    }

    CollectedData(CollectedData&&) = default;

    uint16_t getCID() const { return cid_; }

    const std::vector<char>& getData() const { return data_; }

    StreamBuffer& getBuffer() { return buffer_; }

private:
    std::vector<char> data_;
    StreamBuffer buffer_{data_};
    uint16_t cid_ = 0;
};

} // namespace simdb::collection
