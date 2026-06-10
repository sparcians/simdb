// <Minifiers.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/ArgosResources.hpp"

namespace simdb::argos {

/*!
 * \class Minifier
 * \brief This class greatly reduces the size of the collection database
 * by performing a form of "delta compression". As long as the data has
 * not changed by much (or at all) within a heartbeat interval, a space
 * optimization is typically available. One example would be:
 *
 * -- Scenario:   NOT using minifiers (it still works, but the DB is big)
 * -- Data flow:  CollectionEntryPoint -> PipelineStager
 * -- Collection:
 *
 *       Tick         Heartbeat?    Collect
 *      ------------ ------------- ------------------------
 *       100          Yes           Contig container of <Inst>
 *                                    - Say Inst is a struct with size 24 bytes
 *                                    - At tick 100, we collect when the container
 *                                      had 12 Inst's
 *                                    - Total sent to database:
 *                                        [uint16_t cid]
 *                                        [uint8_t Action::FULL]
 *                                        [uint16_t size (12)]
 *                                        [bytes for Inst at bin 0]
 *                                        [bytes for Inst at bin 1]
 *                                           ...................
 *                                        [bytes for Inst at bin 11]
 *                                      _______________________________________
 *                                        293 bytes collected
 *
 *       101          No            One new Inst was pushed onto the back of the
 *                                  container (assume contiguous). We aren't using
 *                                  minifiers, so we collect the entire thing:
 *
 *                                        [same amount of bytes from tick 100, so 293 bytes]
 *                                        [an extra 24 bytes for the new Inst that was pushed]
 *                                      ________________________________________________________
 *                                        317 bytes collected (610 bytes in total)
 *
 * -- Scenario:   Enable minifiers
 * -- Data flow:  CollectionEntryPoint -> Minifier -> PipelineStager
 * -- Collection:
 *
 *       Tick         Heartbeat?    Collect
 *      ------------ ------------- ------------------------
 *       100          Yes           Contig container of <Inst>, same as above.
 *                                  Recall that the container had 12 Inst's in it
 *                                  at this time. 293 bytes were collected here.
 *
 *       101          No            One new Inst was pushed onto the back of the
 *                                  container (assume contiguous). Since we are
 *                                  using minifiers, we know an optimization for
 *                                  this:
 *                                        [uint16_t cid]
 *                                        [uint8_t Action::CONTIG_CONTAINER_ARRIVE]
 *                                        [24 bytes for the pushed Inst]
 *                                      -----------------------------------------------
 *                                        27 bytes written here (320 bytes in total)
 *
 * So using minifiers would have gone from 610 bytes to 320 bytes to collect
 * the same data. A variety of delta "encodings" are used by the minifiers
 * everytime they are able to. The times they will not be able to are:
 *
 *    - At heartbeats
 *    - When too much has changed since the last collected value (no encoding for it)
 *
 * In these cases, they will default to a full byte dump and rebase.
 *
 * TODO cnyce: These need to be template classes when the collection
 * code from Sparta is moved into SimDB.
 */
class Minifier
{
public:
    // \enum Action
    // \brief Encodings that C++ and python agree on. The python
    // deserializers will know how to interpret the byte stream
    // using the header for all collected data:
    //
    //   [uint16_t cid]             // collectable ID
    //   [uint8_t action]           // encoding
    enum class Action : uint8_t {
        // Common to all collected types
        DISABLED = 0,
        ENABLED,
        QUIETED,
        AWAKENED,
        FULL,
        CARRY,

        // Specific to contiguous containers
        CONTIG_CONTAINER_SWAP,
        CONTIG_CONTAINER_ARRIVE,
        CONTIG_CONTAINER_DEPART,
        CONTIG_CONTAINER_BOOKENDS,

        // Specific to sparse containers:
        SPARSE_CONTAINER_SWAP,
        SPARSE_CONTAINER_REMOVE
    };

    // Create minifier for scalars (including scalar structs)
    Minifier(ArgosResources* resource_container, uint16_t cid, bool full_dump_only = false) :
        stager_(resource_container->getStagerResource()),
        cid_(cid),
        full_dump_only_(full_dump_only)
    {
        prev_scalar_bytes_ = std::vector<char>();
    }

    // Create minifier for containers
    Minifier(ArgosResources* resource_container, uint16_t cid, bool sparse, size_t capacity,
             bool full_dump_only = false) :
        stager_(resource_container->getStagerResource()),
        cid_(cid),
        full_dump_only_(full_dump_only)
    {
        if (!sparse)
        {
            prev_contig_bins_.resize(capacity);
        }
    }

    // Called by CollectionEntryPoint when used to collect
    // scalars (including scalar structs)
    void minifyAndSend(const std::vector<char>& scalar_bytes)
    {
        CollectedData data(cid_);
        auto& buf = data.getBuffer();

        auto write_full =
            // Always dump FULL when minifiers are disabled
            full_dump_only_ ||

            // Always need to set baseline on first collection
            !has_history_ ||

            // Always need to rebase when we hit a heartbeat
            (cycles_since_last_full_ + 1) % stager_->getHeartbeat() == 0 ||

            // Scalars don't have any delta compression algos
            // at this time
            prev_scalar_bytes_ != scalar_bytes;

        if (write_full)
        {
            buf.append(Action::FULL);
            buf.append(scalar_bytes);
            cycles_since_last_full_ = 0;
        } else
        {
            buf.append(Action::CARRY);
            ++cycles_since_last_full_;
        }

        has_history_ = true;
        prev_scalar_bytes_ = scalar_bytes;
        stager_->stage(std::move(data));
    }

    // Called by CollectionEntryPoint when used to collect
    // contiguous containers
    void minifyAndSend(const std::vector<std::vector<char>>& contig_bin_bytes)
    {
        CollectedData data(cid_);
        auto& buf = data.getBuffer();

        auto write_full =
            // Always dump FULL when minifiers are disabled
            full_dump_only_ ||

            // Always need to set baseline on first collection
            !has_history_ ||

            // Always need to rebase when we hit a heartbeat
            (cycles_since_last_full_ + 1) % stager_->getHeartbeat() == 0;

        auto curr_size = getNumElements_(contig_bin_bytes);

        if (write_full)
        {
            buf.append(Action::FULL);
            buf.append(curr_size);
            for (uint16_t i = 0; i < curr_size; ++i)
            {
                buf.append(contig_bin_bytes[i]);
            }
            stager_->stage(std::move(data));
            prev_contig_bins_ = contig_bin_bytes;
            cycles_since_last_full_ = 0;
            return;
        }

        auto prev_size = getNumElements_(prev_contig_bins_);

        // When the size of the container has not changed,
        // we will do one of the following:
        //
        //   - Nothing changed (CARRY)
        //   - Popped one from the front and pushed one to the
        //     back at the same tick (BOOKENDS)
        //   - Exactly one item changed (SWAP)
        //   - More than one item changed (no optimization,
        //     so FULL and rebase)
        if (curr_size == prev_size)
        {
            ValidValue<uint16_t> changed_idx;
            uint16_t num_changes = 0;
            for (uint16_t i = 0; i < curr_size; ++i)
            {
                if (contig_bin_bytes[i] != prev_contig_bins_[i])
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
                buf.append(Action::CARRY);
                stager_->stage(std::move(data));
                ++cycles_since_last_full_;
                return;
            } else if (num_changes == 1)
            {
                buf.append(Action::CONTIG_CONTAINER_SWAP);
                buf.append(contig_bin_bytes[changed_idx]);
                stager_->stage(std::move(data));
                prev_contig_bins_ = contig_bin_bytes;
                ++cycles_since_last_full_;
                return;
            }

            // If more than one item changed, we should check for the
            // one-pushed/one-popped BOOKENDS case.
            if (curr_size > 0)
            {
                bool bookends = true;
                for (size_t i = 0; i + 1 < curr_size; ++i)
                {
                    // BOOKENDS example:
                    //
                    //   previous:    [A,B,C,D]
                    //   current:     [B,C,D,E]
                    if (contig_bin_bytes[i] != prev_contig_bins_[i + 1])
                    {
                        bookends = false;
                        break;
                    }
                }

                if (bookends)
                {
                    buf.append(Action::CONTIG_CONTAINER_BOOKENDS);
                    buf.append(contig_bin_bytes[curr_size - 1]);
                    stager_->stage(std::move(data));
                    prev_contig_bins_ = contig_bin_bytes;
                    ++cycles_since_last_full_;
                    return;
                }
            }
        }

        // Check if we can do the ARRIVE optimization
        else if (curr_size == prev_size + 1)
        {
            bool arrive = true;
            for (uint16_t i = 0; i < prev_size; ++i)
            {
                if (contig_bin_bytes[i] != prev_contig_bins_[i])
                {
                    arrive = false;
                    break;
                }
            }

            if (arrive)
            {
                buf.append(Action::CONTIG_CONTAINER_ARRIVE);
                buf.append(contig_bin_bytes[curr_size - 1]);
                stager_->stage(std::move(data));
                prev_contig_bins_ = contig_bin_bytes;
                ++cycles_since_last_full_;
                return;
            }
        }

        // Check if we can do the DEPART optimization
        else if (prev_size == curr_size + 1)
        {
            bool depart = true;
            for (uint16_t i = 0; i < curr_size; ++i)
            {
                if (contig_bin_bytes[i] != prev_contig_bins_[i + 1])
                {
                    depart = false;
                    break;
                }
            }

            if (depart)
            {
                buf.append(Action::CONTIG_CONTAINER_DEPART);
                stager_->stage(std::move(data));
                prev_contig_bins_ = contig_bin_bytes;
                ++cycles_since_last_full_;
                return;
            }
        }

        // If we got this far, we don't have an optimization to encode
        // the changes.
        buf.append(Action::FULL);
        buf.append(curr_size);
        for (uint16_t i = 0; i < curr_size; ++i)
        {
            buf.append(contig_bin_bytes[i]);
        }
        stager_->stage(std::move(data));
        prev_contig_bins_ = contig_bin_bytes;
        cycles_since_last_full_ = 0;
        return;
    }

    // Called by CollectionEntryPoint when used to collect
    // sparse containers
    void minifyAndSend(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes)
    {
        CollectedData data(cid_);
        auto& buf = data.getBuffer();

        auto write_full =
            // Always dump FULL when minifiers are disabled
            full_dump_only_ ||

            // Always need to set baseline on first collection
            !has_history_ ||

            // Always need to rebase when we hit a heartbeat
            (cycles_since_last_full_ + 1) % stager_->getHeartbeat() == 0;

        auto curr_size = getNumElements_(sparse_bin_bytes);

        if (write_full)
        {
            buf.append(Action::FULL);
            buf.append(curr_size);
            for (const auto& [bin_idx, bin_bytes] : sparse_bin_bytes)
            {
                if (!bin_bytes.empty())
                {
                    buf.append(bin_idx);
                    buf.append(bin_bytes);
                }
            }
            stager_->stage(std::move(data));
            prev_sparse_bins_ = sparse_bin_bytes;
            cycles_since_last_full_ = 0;
            return;
        }

        auto prev_size = getNumElements_(prev_sparse_bins_);

        // When the size of the container has not changed,
        // we will do one of the following:
        //
        //   - Nothing changed (CARRY)
        //   - Exactly one item changed (SWAP)
        //   - More than one item changed (no optimization,
        //     so FULL and rebase)
        if (curr_size == prev_size)
        {
            if (sparse_bin_bytes == prev_sparse_bins_)
            {
                buf.append(Action::CARRY);
                stager_->stage(std::move(data));
                ++cycles_since_last_full_;
                return;
            }

            std::vector<uint16_t> changed_idxs;
            std::vector<uint16_t> removed_idxs;

            auto can_optimize = [&]() { return changed_idxs.size() + removed_idxs.size() > 1; };

            for (const auto& [prev_idx, prev_bytes] : prev_sparse_bins_)
            {
                if (auto it = sparse_bin_bytes.find(prev_idx); it != sparse_bin_bytes.end())
                {
                    if (prev_bytes != it->second)
                    {
                        changed_idxs.push_back(prev_idx);
                        if (!can_optimize())
                        {
                            break;
                        }
                    }
                } else
                {
                    removed_idxs.push_back(prev_idx);
                    if (!can_optimize())
                    {
                        break;
                    }
                }
            }

            if (can_optimize())
            {
                for (const auto& [curr_idx, curr_bytes] : sparse_bin_bytes)
                {
                    if (auto it = prev_sparse_bins_.find(curr_idx); it == prev_sparse_bins_.end())
                    {
                        changed_idxs.push_back(curr_idx);
                        if (!can_optimize())
                        {
                            break;
                        }
                    }
                }
            }

            // Only SWAP if exactly one item changed
            if (changed_idxs.size() == 1 && removed_idxs.empty())
            {
                auto changed_idx = changed_idxs[0];
                buf.append(Action::SPARSE_CONTAINER_SWAP);
                buf.append(sparse_bin_bytes.at(changed_idx));
                stager_->stage(std::move(data));
                prev_sparse_bins_ = sparse_bin_bytes;
                ++cycles_since_last_full_;
            }

            // Only REMOVE if exactly one item removed
            if (removed_idxs.size() == 1 && changed_idxs.empty())
            {
                auto removed_idx = removed_idxs[0];
                buf.append(Action::SPARSE_CONTAINER_REMOVE);
                buf.append(removed_idx);
                stager_->stage(std::move(data));
                prev_sparse_bins_ = sparse_bin_bytes;
                ++cycles_since_last_full_;
                return;
            }

            // If we got this far, we don't have an optimization to encode
            // the changes.
            buf.append(Action::FULL);
            buf.append(curr_size);
            for (const auto& [bin_idx, bin_bytes] : sparse_bin_bytes)
            {
                if (!bin_bytes.empty())
                {
                    buf.append(bin_idx);
                    buf.append(bin_bytes);
                }
            }
            stager_->stage(std::move(data));
            prev_sparse_bins_ = sparse_bin_bytes;
            cycles_since_last_full_ = 0;
        }
    }

private:
    uint16_t getNumElements_(const std::vector<std::vector<char>>& contig_bin_bytes) const
    {
        uint64_t count = 0;
        for (const auto& bytes : contig_bin_bytes)
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

    uint16_t getNumElements_(const std::map<uint16_t, std::vector<char>>& sparse_bin_bytes) const
    {
        uint64_t count = 0;
        for (const auto& [_, bytes] : sparse_bin_bytes)
        {
            if (!bytes.empty())
            {
                ++count;
            }
        }

        assert(count <= UINT16_MAX);
        return count;
    }

    PipelineStagerResource& stager_;
    const uint16_t cid_;
    const bool full_dump_only_;

    bool has_history_ = false;
    size_t cycles_since_last_full_ = 0;
    std::vector<char> prev_scalar_bytes_;
    std::vector<std::vector<char>> prev_contig_bins_;
    std::map<uint16_t, std::vector<char>> prev_sparse_bins_;
};

} // namespace simdb::argos
