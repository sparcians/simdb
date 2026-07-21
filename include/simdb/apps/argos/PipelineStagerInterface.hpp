// <PipelineStagerInterface.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/PipelineDataTypes.hpp"
#include "simdb/utils/TinyStrings.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace simdb::argos {

//! \brief Interface class to receive collected data, notifications, and open/close state changes.
class PipelineStagerInterface
{
public:
    virtual ~PipelineStagerInterface() = default;

    virtual void stage(uint16_t cid, std::vector<char>&& scalar_bytes) = 0;

    virtual void stage(uint16_t cid, std::vector<std::vector<char>>&& contig_bin_bytes) = 0;

    virtual void stage(uint16_t cid, std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes) = 0;

    virtual void closeRecord(uint16_t cid) = 0;

    virtual void postNotif(const std::string& notif, NotifType type) = 0;

    void postWarning(const std::string& warning) { postNotif(warning, NotifType::WARNING); }

    void postError(const std::string& error) { postNotif(error, NotifType::ERROR); }

    void postMessage(const std::string& msg) { postNotif(msg, NotifType::MESSAGE); }
};

} // namespace simdb::argos
