// <CheckpointDeltas.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace simdb::argos {

//! Result of comparing two consecutive scalar payloads (raw bytes, no framing).
enum class ScalarDeltaKind { UNCHANGED, CHANGED };

//! Compare previous and current scalar payloads.
//!
//! Returns CHANGED when \p prev is empty (first collection has no baseline).
//! Scalars only support CARRY when UNCHANGED; CHANGED implies a FULL snapshot.
inline ScalarDeltaKind classifyScalarChange(const std::vector<char>& prev, const std::vector<char>& curr)
{
    if (prev.empty())
    {
        return ScalarDeltaKind::CHANGED;
    }

    return prev == curr ? ScalarDeltaKind::UNCHANGED : ScalarDeltaKind::CHANGED;
}

inline std::ostream& operator<<(std::ostream& os, ScalarDeltaKind kind)
{
    switch (kind)
    {
    case ScalarDeltaKind::UNCHANGED:
        return os << "UNCHANGED";
    case ScalarDeltaKind::CHANGED:
        return os << "CHANGED";
    }
    throw DBException("Invalid ScalarDeltaKind");
}

//! Contiguous-container delta kinds (wire action without heartbeat forcing).
enum class ContigDeltaKind {
    CARRY,
    SWAP,
    BOOKENDS,
    ARRIVE,
    DEPART,
    FULL,
};

//! Result of classifying a contig container transition (no CID/action framing).
struct ContigDeltaClassification
{
    ContigDeltaKind kind = ContigDeltaKind::FULL;
    simdb::ValidValue<uint16_t> swap_index;
    std::vector<char> payload;
};

inline uint16_t countContigElements_(const std::vector<std::vector<char>>& contig_bins)
{
    uint64_t count = 0;
    for (const auto& bytes : contig_bins)
    {
        if (!bytes.empty())
        {
            ++count;
        } else
        {
            break;
        }
    }
    assert(count <= UINT16_MAX);
    return count;
}

//! Classify how a contiguous container changed between consecutive collections.
//!
//! Returns FULL when \p prev is empty (no baseline). Heartbeat forcing is handled
//! by the checkpointer, not this helper.
inline ContigDeltaClassification classifyContigChange(const std::vector<std::vector<char>>& prev,
                                                      const std::vector<std::vector<char>>& curr)
{
    ContigDeltaClassification result;

    if (prev.empty())
    {
        result.kind = ContigDeltaKind::FULL;
        return result;
    }

    const auto curr_size = countContigElements_(curr);
    const auto prev_size = countContigElements_(prev);

    if (curr_size == prev_size)
    {
        ValidValue<uint16_t> changed_idx;
        uint16_t num_changes = 0;
        for (uint16_t i = 0; i < curr_size; ++i)
        {
            if (curr[i] != prev[i])
            {
                changed_idx = i;
                ++num_changes;
                if (num_changes > 1)
                {
                    changed_idx.clearValid();
                    break;
                }
            }
        }

        if (num_changes == 0)
        {
            result.kind = ContigDeltaKind::CARRY;
            return result;
        }

        if (num_changes == 1)
        {
            result.kind = ContigDeltaKind::SWAP;
            result.swap_index = changed_idx.getValue();
            result.payload = curr[changed_idx.getValue()];
            return result;
        }

        if (curr_size > 0)
        {
            bool bookends = true;
            for (size_t i = 0; i + 1 < curr_size; ++i)
            {
                if (curr[i] != prev[i + 1])
                {
                    bookends = false;
                    break;
                }
            }

            if (bookends)
            {
                result.kind = ContigDeltaKind::BOOKENDS;
                result.payload = curr[curr_size - 1];
                return result;
            }
        }
    } else if (curr_size == prev_size + 1)
    {
        bool arrive = true;
        for (uint16_t i = 0; i < prev_size; ++i)
        {
            if (curr[i] != prev[i])
            {
                arrive = false;
                break;
            }
        }

        if (arrive)
        {
            result.kind = ContigDeltaKind::ARRIVE;
            result.payload = curr[curr_size - 1];
            return result;
        }
    } else if (prev_size == curr_size + 1)
    {
        bool depart = true;
        for (uint16_t i = 0; i < curr_size; ++i)
        {
            if (curr[i] != prev[i + 1])
            {
                depart = false;
                break;
            }
        }

        if (depart)
        {
            result.kind = ContigDeltaKind::DEPART;
            return result;
        }
    }

    result.kind = ContigDeltaKind::FULL;
    return result;
}

inline std::ostream& operator<<(std::ostream& os, ContigDeltaKind kind)
{
    switch (kind)
    {
    case ContigDeltaKind::CARRY:
        return os << "CARRY";
    case ContigDeltaKind::SWAP:
        return os << "SWAP";
    case ContigDeltaKind::BOOKENDS:
        return os << "BOOKENDS";
    case ContigDeltaKind::ARRIVE:
        return os << "ARRIVE";
    case ContigDeltaKind::DEPART:
        return os << "DEPART";
    case ContigDeltaKind::FULL:
        return os << "FULL";
    }
    throw DBException("Invalid ContigDeltaKind");
}

} // namespace simdb::argos
