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

//! \brief Holds onto all byte buffers containing collected data and open/close state changes.
//! All records of everything that happened at a given simulation time T are processed together
//! and end up in the DB in the same blob/rowid. Clocks are tracked so we can support multi-clock
//! simulations. The clock_ids are a set (not a scalar) since two clocks can collect at the same
//! time (example: one clock might be exactly twice as fast as another).
//!
//! Used as input to the zlib pipeline stage.
struct QueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    std::vector<std::unique_ptr<CollectedData>> entries;
    std::unordered_set<uint32_t> clock_ids;
};

//! \brief Output data structure from the zlib pipeline stage.
struct CompressedQueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    std::vector<char> compressed_collection_data;
    std::unordered_set<uint32_t> clock_ids;
};

//! \brief Enum used for warning/error/message filtering in Argos.
enum class NotifType { WARNING, ERROR, MESSAGE, __INVALID__ };

//! \brief Notification captured during collection. These appear in a dedicated tab in Argos.
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

//! \brief Single byte buffer with CID for scalar collectables (including scalar structs).
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

//! \brief Vector of byte buffers with CID for contig container collectables.
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

//! \brief Bin-mapped byte buffers with CID for sparse container collectables.
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

//! \brief Timestamped notification.
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

//! \brief This class is used to create all the *Entries above. Tracks everything that happened
//! across all collectables at a specific simulation time point. A ledger is filled at time T
//! until simulation has moved forward, then the ledger is moved to the 1st async stage of
//! the DB pipeline. ArgosCollector will create a new Ledger and add to it until time advances
//! again.
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

    void closeRecord(uint16_t cid) { closed_cids_.insert(cid); }

    uint64_t getSimTime() const { return sim_time_; }

    uint64_t getWindowId() const { return window_id_; }

    bool hasEntries() const
    {
        return !scalar_records_.empty() || !contig_records_.empty() || !sparse_records_.empty() ||
               !closed_cids_.empty();
    }

    std::vector<ScalarEntry> releaseScalarEntries() { return std::move(scalar_records_); }

    std::vector<ContigEntry> releaseContigEntries() { return std::move(contig_records_); }

    std::vector<SparseEntry> releaseSparseEntries() { return std::move(sparse_records_); }

    const std::unordered_set<uint16_t>& getClosedCIDs() const { return closed_cids_; }

private:
    uint64_t sim_time_ = 0;
    uint64_t window_id_ = 0;
    std::vector<ScalarEntry> scalar_records_;
    std::vector<ContigEntry> contig_records_;
    std::vector<SparseEntry> sparse_records_;
    std::unordered_set<uint16_t> closed_cids_;
};

using LedgerPtr = std::unique_ptr<Ledger>;

} // namespace simdb::argos
