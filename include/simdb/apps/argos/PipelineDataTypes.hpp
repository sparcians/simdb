// <PipelineDataTypes.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/utils/Demangle.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace simdb::argos {

using CollectionDataAtTimePoint = std::vector<std::unique_ptr<CollectedData>>;
using EnabledChangedAtTimePoint = std::vector<std::pair<uint16_t, bool>>;
using QuietChangedAtTimePoint = std::vector<std::pair<uint16_t, bool>>;

struct QueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    CollectionDataAtTimePoint collection_data;
    EnabledChangedAtTimePoint enabled_changes;
    QuietChangedAtTimePoint quiet_changes;
};

struct CompressedQueueCollectionData
{
    ValidValue<uint64_t> sim_time;
    std::vector<char> compressed_collection_data;
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
