// <CheckpointerSparse.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointerBase.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"

#include <cassert>
#include <map>
#include <vector>

namespace simdb::argos {

class SparseSnapshotCheckpoint : public Checkpoint
{
public:
    SparseSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent,
                             std::map<uint16_t, std::vector<char>> bins) :
        cid_(cid),
        parent_(std::move(parent)),
        bins_(std::move(bins))
    {
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override { return getFullData(); }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(Action::FULL);
        buf.append(encodeSparseFullTail_(bins_));
        return data;
    }

    bool isSnapshot() const override { return true; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return Action::FULL; }

    void detachFromParent() override { parent_.reset(); }

    const std::map<uint16_t, std::vector<char>>& bins() const { return bins_; }

private:
    static std::vector<char> encodeSparseFullTail_(const std::map<uint16_t, std::vector<char>>& bins)
    {
        std::vector<char> tail;
        StreamBuffer buf(tail);

        const auto size = countSparseElements_(bins);
        buf.append(size);
        for (const auto& [bin_idx, bin_bytes] : bins)
        {
            if (!bin_bytes.empty())
            {
                buf.append(bin_idx);
                buf.append(bin_bytes);
            }
        }
        return tail;
    }

    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    std::map<uint16_t, std::vector<char>> bins_;
};

class SparseDeltaCheckpoint : public Checkpoint
{
public:
    SparseDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action,
                          const simdb::ValidValue<uint16_t>& bin_index, std::vector<char> payload) :
        cid_(cid),
        parent_(std::move(parent)),
        action_(action),
        payload_(std::move(payload))
    {
        assert(parent_ != nullptr);
        if (bin_index.isValid())
        {
            bin_index_ = bin_index.getValue();
        }
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(action_);
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
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        const auto parent_bins = reconstituteSparseBins_(*parent_);
        const auto full_bins = applySparseDelta_(parent_bins, action_, bin_index_, payload_);
        auto snapshot = SparseSnapshotCheckpoint(cid_, nullptr, full_bins);
        return snapshot.getFullData();
    }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return action_; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

private:
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

    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    Action action_;
    simdb::ValidValue<uint16_t> bin_index_;
    std::vector<char> payload_;
};

//! Per-sparse-CID checkpoint chain builder.
class SparseCheckpointer : public CheckpointerBase
{
public:
    using CheckpointerBase::CheckpointerBase;

    std::shared_ptr<Checkpoint> createCheckpoint(const std::map<uint16_t, std::vector<char>>& curr)
    {
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
};

} // namespace simdb::argos
