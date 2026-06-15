// <PipelineDataTypes.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

//! This file contains data types that are used to pass information
//! down ArgosCollector's pipeline to the database.

namespace simdb::argos {

class CollectableCheckpoint;

enum class CheckpointerKind { Scalar, Contig, Sparse };

enum class EncodePath { ActiveAnchor, AbsentRefresh };

struct CidEncodeWork
{
    uint16_t cid = 0;
    CheckpointerKind kind = CheckpointerKind::Scalar;
    EncodePath path = EncodePath::ActiveAnchor;
    size_t heartbeat = 0;

    //! ActiveAnchor: shared alias to [anchor .. head] until completion; sever deferred to commitAsyncEncode.
    //! AbsentRefresh: read-only alias to live head_ (encoder must not mutate the chain).
    std::shared_ptr<CollectableCheckpoint> stolen_chain_tail;
    CollectableCheckpoint* anchor = nullptr;

    bool tip_disabled = false;
};

struct DeltaEncodingBatch
{
    uint64_t sim_time = 0;
    uint64_t window_id = 0;
    std::vector<CidEncodeWork> work;
    std::unordered_map<uint16_t, uint32_t> cid_to_clock;
};

struct AsyncEncodeCompletion
{
    uint16_t cid = 0;
    std::shared_ptr<CollectableCheckpoint> tail;
    bool release_through_full_anchor = false;
};

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

enum class MinimalType {
    bool_t,
    int8_t,
    int16_t,
    int32_t,
    int64_t,
    uint8_t,
    uint16_t,
    uint32_t,
    uint64_t,
    float_t,
    double_t,
    string_t
};

inline std::ostream& operator<<(std::ostream& os, const MinimalType min_type)
{
    switch (min_type)
    {
    case MinimalType::bool_t:
        return os << demangle_type<bool>();
    case MinimalType::int8_t:
        return os << demangle_type<int8_t>();
    case MinimalType::int16_t:
        return os << demangle_type<int16_t>();
    case MinimalType::int32_t:
        return os << demangle_type<int32_t>();
    case MinimalType::int64_t:
        return os << demangle_type<int64_t>();
    case MinimalType::uint8_t:
        return os << demangle_type<uint8_t>();
    case MinimalType::uint16_t:
        return os << demangle_type<uint16_t>();
    case MinimalType::uint32_t:
        return os << demangle_type<uint32_t>();
    case MinimalType::uint64_t:
        return os << demangle_type<uint64_t>();
    case MinimalType::float_t:
        return os << demangle_type<float>();
    case MinimalType::double_t:
        return os << demangle_type<double>();
    case MinimalType::string_t:
        return os << "string";
    }
    throw DBException("Invalid MinimalType");
}

struct DynamicFieldChanges
{
    ValidValue<uint16_t> cid;
    std::vector<std::string> field_names;
    std::vector<MinimalType> field_types;
    ValidValue<uint64_t> sim_time;

    DynamicFieldChanges(uint16_t cid, const std::vector<std::string>& field_names,
                        const std::vector<MinimalType>& field_types, uint64_t sim_time) :
        cid(cid),
        field_names(field_names),
        field_types(field_types),
        sim_time(sim_time)
    {
    }

    // Default ctor needed for ConcurrentQueue::try_pop
    DynamicFieldChanges() = default;
};

} // namespace simdb::argos
