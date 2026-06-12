// <CheckpointerSparse.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointDeltas.hpp"
#include "simdb/apps/argos/CheckpointNodeBase.hpp"
#include "simdb/apps/argos/CheckpointerBase.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <vector>

namespace simdb::argos {

class SparseSnapshotCheckpoint : public SnapshotCheckpointBase
{
public:
    SparseSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent,
                             std::map<uint16_t, std::vector<char>> bins) :
        SnapshotCheckpointBase(cid, std::move(parent)),
        bins_(std::move(bins))
    {
    }

    const std::map<uint16_t, std::vector<char>>& bins() const { return bins_; }

private:
    void appendFullTail_(StreamBuffer& buf) const override
    {
        const auto size = countSparseElements_(bins_);
        buf.append(size);
        for (const auto& [bin_idx, bin_bytes] : bins_)
        {
            if (!bin_bytes.empty())
            {
                buf.append(bin_idx);
                buf.append(bin_bytes);
            }
        }
    }

    std::map<uint16_t, std::vector<char>> bins_;
};

class SparseDeltaCheckpoint : public IndexedDeltaCheckpointBase
{
public:
    SparseDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action,
                          const simdb::ValidValue<uint16_t>& bin_index, std::vector<char> payload) :
        IndexedDeltaCheckpointBase(cid, std::move(parent), action, bin_index, std::move(payload))
    {
    }

private:
    void appendMinifiedTail_(StreamBuffer& buf) const override
    {
        if (action_ == Action::SPARSE_CONTAINER_SWAP || action_ == Action::SPARSE_CONTAINER_REMOVE)
        {
            assert(bin_index_.isValid());
            buf.append(bin_index_.getValue());
        }
        if (action_ == Action::SPARSE_CONTAINER_SWAP)
        {
            assert(!payload_.empty());
            buf.append(payload_);
        }
    }

    std::unique_ptr<CollectedData> makeFullData_() const override
    {
        const auto parent_bins = reconstituteSparseBins_(*parent_);
        const auto full_bins = applySparseDelta_(parent_bins, action_, bin_index_, payload_);
        auto snapshot = SparseSnapshotCheckpoint(cid_, nullptr, full_bins);
        return snapshot.getFullData();
    }

    static std::map<uint16_t, std::vector<char>>
    applySparseDelta_(const std::map<uint16_t, std::vector<char>>& parent_bins, Action action,
                      simdb::ValidValue<uint16_t> bin_index, const std::vector<char>& payload)
    {
        auto bins = parent_bins;
        switch (action)
        {
        case Action::CARRY:
            break;
        case Action::SPARSE_CONTAINER_SWAP:
            assert(bin_index.isValid());
            assert(!payload.empty());
            bins[bin_index.getValue()] = payload;
            break;
        case Action::SPARSE_CONTAINER_REMOVE:
            assert(bin_index.isValid());
            bins.erase(bin_index.getValue());
            break;
        default:
            throw DBException("Invalid sparse delta action");
        }
        return bins;
    }

    static std::map<uint16_t, std::vector<char>> reconstituteSparseBins_(const Checkpoint& checkpoint)
    {
        if (auto* snapshot = dynamic_cast<const SparseSnapshotCheckpoint*>(&checkpoint))
        {
            return snapshot->bins();
        }
        if (auto* delta = dynamic_cast<const SparseDeltaCheckpoint*>(&checkpoint))
        {
            const auto parent_bins = reconstituteSparseBins_(*delta->parent_);
            return applySparseDelta_(parent_bins, delta->action_, delta->bin_index_, delta->payload_);
        }
        if (auto* vanished = dynamic_cast<const ScalarVanishedCheckpoint*>(&checkpoint))
        {
            return reconstituteSparseBins_(*vanished->parent());
        }
        throw DBException("Cannot reconstitute sparse bins from checkpoint");
    }
};

//! Per-sparse-CID checkpoint chain builder.
class SparseCheckpointer : public CheckpointerBase
{
public:
    using CheckpointerBase::CheckpointerBase;

    std::shared_ptr<Checkpoint> createCheckpoint(const std::map<uint16_t, std::vector<char>>& curr)
    {
        max_container_size_seen_ = std::max(max_container_size_seen_, static_cast<size_t>(countSparseElements_(curr)));

        const bool force_full = isHeartbeatBoundary_();
        const auto classification = classifySparseChange(prev_sparse_bins_, curr);

        std::shared_ptr<Checkpoint> checkpoint;
        if (classification.kind == SparseDeltaKind::FULL || force_full)
        {
            checkpoint = std::make_shared<SparseSnapshotCheckpoint>(cid_, tip_, curr);
        } else
        {
            checkpoint = makeDeltaCheckpoint_(classification);
        }

        tip_ = checkpoint;
        prev_sparse_bins_ = curr;
        return checkpoint;
    }

    size_t getMaxContainerSizeSeen() const { return max_container_size_seen_; }

private:
    static Action sparseActionFromKind_(SparseDeltaKind kind)
    {
        switch (kind)
        {
        case SparseDeltaKind::CARRY:
            return Action::CARRY;
        case SparseDeltaKind::SWAP:
            return Action::SPARSE_CONTAINER_SWAP;
        case SparseDeltaKind::REMOVE:
            return Action::SPARSE_CONTAINER_REMOVE;
        case SparseDeltaKind::FULL:
            break;
        }
        throw DBException("Invalid sparse delta kind");
    }

    std::shared_ptr<Checkpoint> makeDeltaCheckpoint_(const SparseDeltaClassification& classification)
    {
        return std::make_shared<SparseDeltaCheckpoint>(cid_, tip_, sparseActionFromKind_(classification.kind),
                                                       classification.bin_index, classification.payload);
    }

    std::shared_ptr<Checkpoint> makeCarryCheckpoint_() override
    {
        return std::make_shared<SparseDeltaCheckpoint>(cid_, tip_, Action::CARRY, simdb::ValidValue<uint16_t>{},
                                                       std::vector<char>{});
    }

    std::shared_ptr<Checkpoint> makeRootSnapshotAfterWireFull_(const CollectedData& /*full*/) override
    {
        return std::make_shared<SparseSnapshotCheckpoint>(cid_, nullptr, prev_sparse_bins_);
    }

    std::shared_ptr<Checkpoint> makeReenabledSnapshot_() override
    {
        return std::make_shared<SparseSnapshotCheckpoint>(cid_, tip_, prev_sparse_bins_);
    }

    std::map<uint16_t, std::vector<char>> prev_sparse_bins_;
    size_t max_container_size_seen_ = 0;
};

} // namespace simdb::argos
