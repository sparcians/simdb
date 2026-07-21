// <CheckpointDeltas.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

namespace simdb::argos {

//! Result of comparing two consecutive scalar payloads.
enum class ScalarDeltaKind { UNCHANGED, CHANGED };

//! Compare previous and current scalar payloads.
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

//! Contiguous-container delta kinds.
enum class ContigDeltaKind { CARRY, SWAP, MULTI_SWAP, BOOKENDS, ARRIVE, DEPART, MIMO, FULL };

//! Result of classifying a contig container transition.
struct ContigDeltaClassification
{
    ContigDeltaKind kind = ContigDeltaKind::FULL;
    simdb::ValidValue<uint16_t> swap_index;
    std::vector<char> payload;
    uint8_t depart_count = 0;
    uint8_t arrive_count = 0;
    std::vector<std::vector<char>> arrive_payloads;
    std::vector<uint16_t> swap_indices;
    std::vector<std::vector<char>> swap_payloads;
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

inline bool contigPrefixMatchesAfterDepart(const std::vector<std::vector<char>>& prev,
                                           const std::vector<std::vector<char>>& curr, uint16_t prev_size,
                                           uint16_t curr_size, uint16_t depart_count)
{
    const auto overlap = static_cast<uint16_t>(prev_size - depart_count);
    if (overlap + static_cast<uint16_t>(curr_size - overlap) != curr_size)
    {
        return false;
    }
    for (uint16_t i = 0; i < overlap; ++i)
    {
        if (curr[i] != prev[i + depart_count])
        {
            return false;
        }
    }
    return true;
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
        std::vector<uint16_t> changed_idxs;
        for (uint16_t i = 0; i < curr_size; ++i)
        {
            if (curr[i] != prev[i])
            {
                changed_idxs.push_back(i);
                changed_idx = i;
                ++num_changes;
                if (num_changes > 1)
                {
                    changed_idx.clearValid();
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

        result.kind = ContigDeltaKind::MULTI_SWAP;
        result.swap_indices = std::move(changed_idxs);
        result.swap_payloads.reserve(result.swap_indices.size());
        for (const auto idx : result.swap_indices)
        {
            result.swap_payloads.push_back(curr[idx]);
        }
        return result;
    }

    if (curr_size == prev_size + 1)
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

    for (uint16_t depart_count = 0; depart_count <= prev_size; ++depart_count)
    {
        const int64_t arrive_count64 = static_cast<int64_t>(curr_size) - static_cast<int64_t>(prev_size) + depart_count;
        if (arrive_count64 < 0 || arrive_count64 > UINT8_MAX)
        {
            continue;
        }

        const auto arrive_count = static_cast<uint8_t>(arrive_count64);
        if (depart_count + arrive_count <= 1)
        {
            continue;
        }
        if (depart_count == 1 && arrive_count == 1 && curr_size == prev_size)
        {
            continue;
        }

        if (!contigPrefixMatchesAfterDepart(prev, curr, prev_size, curr_size, depart_count))
        {
            continue;
        }

        const auto overlap = static_cast<uint16_t>(prev_size - depart_count);
        result.kind = ContigDeltaKind::MIMO;
        result.depart_count = static_cast<uint8_t>(depart_count);
        result.arrive_count = arrive_count;
        result.arrive_payloads.reserve(arrive_count);
        for (uint8_t j = 0; j < arrive_count; ++j)
        {
            result.arrive_payloads.push_back(curr[overlap + j]);
        }
        return result;
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
    case ContigDeltaKind::MULTI_SWAP:
        return os << "MULTI_SWAP";
    case ContigDeltaKind::BOOKENDS:
        return os << "BOOKENDS";
    case ContigDeltaKind::ARRIVE:
        return os << "ARRIVE";
    case ContigDeltaKind::DEPART:
        return os << "DEPART";
    case ContigDeltaKind::MIMO:
        return os << "MIMO";
    case ContigDeltaKind::FULL:
        return os << "FULL";
    }
    throw DBException("Invalid ContigDeltaKind");
}

//! Sparse-container delta kinds.
enum class SparseDeltaKind { CARRY, SWAP, MULTI_SWAP, REMOVE, ADD, MULTI_REMOVE, FULL };

//! Result of classifying a sparse container transition.
struct SparseDeltaClassification
{
    SparseDeltaKind kind = SparseDeltaKind::FULL;
    simdb::ValidValue<uint16_t> bin_index;
    std::vector<char> payload;
    std::vector<uint16_t> bin_indices;
    std::vector<std::vector<char>> payloads;
};

inline uint16_t countSparseElements(const std::map<uint16_t, std::vector<char>>& sparse_bins)
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

    const auto curr_size = countSparseElements(curr);
    const auto prev_size = countSparseElements(prev);

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

    if (curr_size == prev_size + 1)
    {
        ValidValue<uint16_t> added_idx;
        uint16_t added_count = 0;
        bool other_change = false;
        for (const auto& [curr_idx, curr_bytes] : curr)
        {
            if (auto it = prev.find(curr_idx); it == prev.end())
            {
                ++added_count;
                added_idx = curr_idx;
                result.payload = curr_bytes;
            } else if (it->second != curr_bytes)
            {
                other_change = true;
                break;
            }
        }

        if (!other_change && added_count == 1)
        {
            for (const auto& [prev_idx, _] : prev)
            {
                if (curr.find(prev_idx) == curr.end())
                {
                    other_change = true;
                    break;
                }
            }
        }

        if (!other_change && added_count == 1)
        {
            result.kind = SparseDeltaKind::ADD;
            result.bin_index = added_idx.getValue();
            return result;
        }
    }

    if (curr_size + 1 <= prev_size)
    {
        std::vector<uint16_t> removed_idxs;
        bool other_change = false;
        for (const auto& [prev_idx, prev_bytes] : prev)
        {
            if (auto it = curr.find(prev_idx); it == curr.end())
            {
                removed_idxs.push_back(prev_idx);
            } else if (prev_bytes != it->second)
            {
                other_change = true;
                break;
            }
        }

        if (!other_change && removed_idxs.size() >= 2 &&
            removed_idxs.size() == static_cast<size_t>(prev_size - curr_size))
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

        if (!other_change && removed_idxs.size() >= 2 &&
            removed_idxs.size() == static_cast<size_t>(prev_size - curr_size))
        {
            result.kind = SparseDeltaKind::MULTI_REMOVE;
            result.bin_indices = std::move(removed_idxs);
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
    std::vector<uint16_t> added_idxs;

    for (const auto& [prev_idx, prev_bytes] : prev)
    {
        if (auto it = curr.find(prev_idx); it != curr.end())
        {
            if (prev_bytes != it->second)
            {
                changed_idxs.push_back(prev_idx);
            }
        } else
        {
            removed_idxs.push_back(prev_idx);
        }
    }

    for (const auto& [curr_idx, _] : curr)
    {
        if (prev.find(curr_idx) == prev.end())
        {
            added_idxs.push_back(curr_idx);
        }
    }

    if (!added_idxs.empty() || !removed_idxs.empty())
    {
        if (added_idxs.size() == 1 && removed_idxs.empty() && changed_idxs.empty())
        {
            result.kind = SparseDeltaKind::ADD;
            result.bin_index = added_idxs[0];
            result.payload = curr.at(added_idxs[0]);
            return result;
        }

        if (removed_idxs.size() == 1 && added_idxs.empty() && changed_idxs.empty())
        {
            result.kind = SparseDeltaKind::REMOVE;
            result.bin_index = removed_idxs[0];
            return result;
        }

        if (removed_idxs.size() >= 2 && added_idxs.empty() && changed_idxs.empty())
        {
            result.kind = SparseDeltaKind::MULTI_REMOVE;
            result.bin_indices = std::move(removed_idxs);
            return result;
        }

        result.kind = SparseDeltaKind::FULL;
        return result;
    }

    if (changed_idxs.size() == 1)
    {
        result.kind = SparseDeltaKind::SWAP;
        result.bin_index = changed_idxs[0];
        result.payload = curr.at(changed_idxs[0]);
        return result;
    }

    if (changed_idxs.size() >= 2)
    {
        result.kind = SparseDeltaKind::MULTI_SWAP;
        result.bin_indices = std::move(changed_idxs);
        result.payloads.reserve(result.bin_indices.size());
        for (const auto idx : result.bin_indices)
        {
            result.payloads.push_back(curr.at(idx));
        }
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
    case SparseDeltaKind::MULTI_SWAP:
        return os << "MULTI_SWAP";
    case SparseDeltaKind::REMOVE:
        return os << "REMOVE";
    case SparseDeltaKind::ADD:
        return os << "ADD";
    case SparseDeltaKind::MULTI_REMOVE:
        return os << "MULTI_REMOVE";
    case SparseDeltaKind::FULL:
        return os << "FULL";
    }
    throw DBException("Invalid SparseDeltaKind");
}

} // namespace simdb::argos
