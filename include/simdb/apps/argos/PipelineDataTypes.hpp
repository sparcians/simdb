// <PipelineDataTypes.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/apps/argos/Timestamps.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

//! This file contains data types that are used to pass information
//! down ArgosCollector's pipeline to the database.

namespace simdb::argos {

struct QueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    std::vector<std::unique_ptr<CollectedData>> entries;
    std::unordered_set<uint32_t> clock_ids;
};

struct CompressedQueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    std::vector<char> compressed_collection_data;
    std::unordered_set<uint32_t> clock_ids;
};

enum class NotifType { WARNING, ERROR, MESSAGE, __INVALID__ };

struct Notification
{
    ValidValue<uint16_t> cid;
    std::string notif;
    NotifType type = NotifType::__INVALID__;
    ValidValue<uint64_t> sim_time;

    Notification(uint16_t cid, const std::string& notif, const NotifType type, uint64_t sim_time) :
        cid(cid),
        notif(notif),
        type(type),
        sim_time(sim_time)
    {
    }

    Notification(uint16_t cid, const std::string& notif, const NotifType type) :
        cid(cid),
        notif(notif),
        type(type)
    {
    }

    // Default ctor needed for ConcurrentQueue::try_pop
    Notification() = default;
};

struct ScalarEntry
{
    uint16_t cid = 0;
    std::vector<char> scalar_bytes;

    ScalarEntry(uint16_t cid, std::vector<char>&& scalar_bytes) :
        cid(cid),
        scalar_bytes(std::move(scalar_bytes))
    {
    }

    ScalarEntry() = default;
    ScalarEntry(ScalarEntry&&) = default;
    ScalarEntry& operator=(ScalarEntry&&) = default;
};

struct ContigEntry
{
    uint16_t cid = 0;
    std::vector<std::vector<char>> contig_bin_bytes;

    ContigEntry(uint16_t cid, std::vector<std::vector<char>>&& contig_bin_bytes) :
        cid(cid),
        contig_bin_bytes(std::move(contig_bin_bytes))
    {
    }

    ContigEntry() = default;
    ContigEntry(ContigEntry&&) = default;
    ContigEntry& operator=(ContigEntry&&) = default;
};

struct SparseEntry
{
    uint16_t cid = 0;
    std::map<uint16_t, std::vector<char>> sparse_bin_bytes;

    SparseEntry(uint16_t cid, std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes) :
        cid(cid),
        sparse_bin_bytes(std::move(sparse_bin_bytes))
    {
    }

    SparseEntry() = default;
    SparseEntry(SparseEntry&&) = default;
    SparseEntry& operator=(SparseEntry&&) = default;
};

struct NotifEntry
{
    ValidValue<uint64_t> sim_time;
    std::string notif;
    NotifType type = NotifType::__INVALID__;

    NotifEntry(const Timestamp* timestamp, const std::string& notif, NotifType type) :
        notif(notif),
        type(type)
    {
        if (timestamp)
        {
            sim_time = timestamp->getTime();
        }
    }

    NotifEntry() = default;
    NotifEntry(NotifEntry&&) = default;
    NotifEntry& operator=(NotifEntry&&) = default;
};

class Ledger
{
public:
    Ledger(uint64_t sim_time, uint64_t window_id, uint64_t reserve_num_scalars = 0, uint64_t reserve_num_contigs = 0,
           uint64_t reserve_num_sparses = 0) :
        sim_time_(sim_time),
        window_id_(window_id)
    {
        scalar_records_.reserve(reserve_num_scalars);
        contig_records_.reserve(reserve_num_contigs);
        sparse_records_.reserve(reserve_num_sparses);
    }

    Ledger(Ledger&&) = default;
    Ledger(const Ledger&) = delete;

    void recordScalar(uint16_t cid, std::vector<char>&& scalar_bytes)
    {
        scalar_records_.emplace_back(cid, std::move(scalar_bytes));
    }

    void recordContig(uint16_t cid, std::vector<std::vector<char>>&& contig_bytes)
    {
        contig_records_.emplace_back(cid, std::move(contig_bytes));
    }

    void recordSparse(uint16_t cid, std::map<uint16_t, std::vector<char>>&& sparse_bin_bytes)
    {
        sparse_records_.emplace_back(cid, std::move(sparse_bin_bytes));
    }

    void recordLifecycleChange(uint16_t cid, bool closed) { closed_states_[cid] = closed; }

    uint64_t getSimTime() const { return sim_time_; }

    uint64_t getWindowId() const { return window_id_; }

    bool hasEntries() const
    {
        return !scalar_records_.empty() || !contig_records_.empty() || !sparse_records_.empty() ||
               !closed_states_.empty();
    }

    std::vector<ScalarEntry> releaseScalarEntries() { return std::move(scalar_records_); }

    std::vector<ContigEntry> releaseContigEntries() { return std::move(contig_records_); }

    std::vector<SparseEntry> releaseSparseEntries() { return std::move(sparse_records_); }

    const std::unordered_map<uint16_t, bool>& getClosedStates() const { return closed_states_; }

private:
    uint64_t sim_time_ = 0;
    uint64_t window_id_ = 0;
    std::vector<ScalarEntry> scalar_records_;
    std::vector<ContigEntry> contig_records_;
    std::vector<SparseEntry> sparse_records_;
    std::unordered_map<uint16_t, bool> closed_states_;
};

using LedgerPtr = std::unique_ptr<Ledger>;

} // namespace simdb::argos
