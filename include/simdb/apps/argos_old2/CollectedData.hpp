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
    explicit CollectedData(uint16_t cid)
        : cid_(cid)
    {
        buffer_.appendValue(cid_, "cid", std::to_string(static_cast<unsigned>(cid_)));
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

} // namespace simdb::argos
