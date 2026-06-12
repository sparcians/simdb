// <CheckpointDeltas.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <cstdint>
#include <iostream>
#include <map>
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

inline uint16_t countContigElements(const std::vector<std::vector<char>>& contig_bins)
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

    const auto curr_size = countContigElements(curr);
    const auto prev_size = countContigElements(prev);

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

//! Sparse-container delta kinds (wire action without heartbeat forcing).
enum class SparseDeltaKind {
    CARRY,
    SWAP,
    REMOVE,
    FULL,
};

//! Result of classifying a sparse container transition (no CID/action framing).
struct SparseDeltaClassification
{
    SparseDeltaKind kind = SparseDeltaKind::FULL;
    simdb::ValidValue<uint16_t> bin_index;
    std::vector<char> payload;
};

inline uint16_t countSparseElements_(const std::map<uint16_t, std::vector<char>>& sparse_bins)
{
    uint64_t count = 0;
    for (const auto& [_, bytes] : sparse_bins)
    {
        if (!bytes.empty())
        {
            ++count;
        }
    }
    assert(count <= UINT16_MAX);
    return static_cast<uint16_t>(count);
}

//! Classify how a sparse container changed between consecutive collections.
//!
//! Returns FULL when \p prev is empty (no baseline). Heartbeat forcing is handled
//! by the checkpointer, not this helper.
inline SparseDeltaClassification classifySparseChange(const std::map<uint16_t, std::vector<char>>& prev,
                                                      const std::map<uint16_t, std::vector<char>>& curr)
{
    SparseDeltaClassification result;

    if (prev.empty())
    {
        result.kind = SparseDeltaKind::FULL;
        return result;
    }

    const auto curr_size = countSparseElements_(curr);
    const auto prev_size = countSparseElements_(prev);

    if (curr == prev)
    {
        result.kind = SparseDeltaKind::CARRY;
        return result;
    }

    if (curr_size + 1 == prev_size)
    {
        ValidValue<uint16_t> removed_idx;
        uint16_t removed_count = 0;
        bool other_change = false;
        for (const auto& [prev_idx, prev_bytes] : prev)
        {
            if (auto it = curr.find(prev_idx); it == curr.end())
            {
                ++removed_count;
                removed_idx = prev_idx;
            } else if (prev_bytes != it->second)
            {
                other_change = true;
                break;
            }
        }

        if (!other_change && removed_count == 1)
        {
            for (const auto& [curr_idx, _] : curr)
            {
                if (prev.find(curr_idx) == prev.end())
                {
                    other_change = true;
                    break;
                }
            }
        }

        if (!other_change && removed_count == 1)
        {
            result.kind = SparseDeltaKind::REMOVE;
            result.bin_index = removed_idx.getValue();
            return result;
        }
    }

    if (curr_size != prev_size)
    {
        result.kind = SparseDeltaKind::FULL;
        return result;
    }

    std::vector<uint16_t> changed_idxs;
    std::vector<uint16_t> removed_idxs;

    auto too_many_changes = [&]() { return changed_idxs.size() + removed_idxs.size() > 1; };

    for (const auto& [prev_idx, prev_bytes] : prev)
    {
        if (auto it = curr.find(prev_idx); it != curr.end())
        {
            if (prev_bytes != it->second)
            {
                changed_idxs.push_back(prev_idx);
                if (too_many_changes())
                {
                    break;
                }
            }
        } else
        {
            removed_idxs.push_back(prev_idx);
            if (too_many_changes())
            {
                break;
            }
        }
    }

    if (too_many_changes())
    {
        for (const auto& [curr_idx, curr_bytes] : curr)
        {
            if (prev.find(curr_idx) == prev.end())
            {
                changed_idxs.push_back(curr_idx);
                if (too_many_changes())
                {
                    break;
                }
            }
        }
    }

    if (changed_idxs.size() == 1 && removed_idxs.empty())
    {
        result.kind = SparseDeltaKind::SWAP;
        result.bin_index = changed_idxs[0];
        result.payload = curr.at(changed_idxs[0]);
        return result;
    }

    if (removed_idxs.size() == 1 && changed_idxs.empty())
    {
        result.kind = SparseDeltaKind::REMOVE;
        result.bin_index = removed_idxs[0];
        return result;
    }

    result.kind = SparseDeltaKind::FULL;
    return result;
}

inline std::ostream& operator<<(std::ostream& os, SparseDeltaKind kind)
{
    switch (kind)
    {
    case SparseDeltaKind::CARRY:
        return os << "CARRY";
    case SparseDeltaKind::SWAP:
        return os << "SWAP";
    case SparseDeltaKind::REMOVE:
        return os << "REMOVE";
    case SparseDeltaKind::FULL:
        return os << "FULL";
    }
    throw DBException("Invalid SparseDeltaKind");
}

} // namespace simdb::argos
