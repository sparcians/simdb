// <CheckpointerContig.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointDeltas.hpp"
#include "simdb/apps/argos/CheckpointNodeBase.hpp"
#include "simdb/apps/argos/CheckpointerBase.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

namespace simdb::argos {

class ContigSnapshotCheckpoint : public SnapshotCheckpointBase
{
public:
    ContigSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, std::vector<std::vector<char>> bins) :
        SnapshotCheckpointBase(cid, std::move(parent)),
        bins_(std::move(bins))
    {
    }

    const std::vector<std::vector<char>>& bins() const { return bins_; }

private:
    void appendFullTail_(StreamBuffer& buf) const override
    {
        const auto size = countContigElements(bins_);
        buf.append(size);
        for (uint16_t i = 0; i < size; ++i)
        {
            buf.append(bins_[i]);
        }
    }

    std::vector<std::vector<char>> bins_;
};

class ContigDeltaCheckpoint : public IndexedDeltaCheckpointBase
{
public:
    ContigDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action,
                          const simdb::ValidValue<uint16_t>& swap_index, std::vector<char> payload) :
        IndexedDeltaCheckpointBase(cid, std::move(parent), action, swap_index, std::move(payload))
    {
    }

private:
    void appendMinifiedTail_(StreamBuffer& buf) const override
    {
        if (action_ == Action::CONTIG_CONTAINER_SWAP)
        {
            assert(bin_index_.isValid());
            assert(!payload_.empty());
            buf.append(bin_index_.getValue());
            buf.append(payload_);
        } else if (action_ == Action::CONTIG_CONTAINER_ARRIVE || action_ == Action::CONTIG_CONTAINER_BOOKENDS)
        {
            assert(!payload_.empty());
            buf.append(payload_);
        }
    }

    std::unique_ptr<CollectedData> makeFullData_() const override
    {
        const auto parent_bins = reconstituteContigBins_(*parent_);
        const auto full_bins = applyContigDelta_(parent_bins, action_, bin_index_, payload_);
        auto snapshot = ContigSnapshotCheckpoint(cid_, nullptr, full_bins);
        return snapshot.getFullData();
    }

    static std::vector<std::vector<char>> denseBins_(const std::vector<std::vector<char>>& bins)
    {
        std::vector<std::vector<char>> dense;
        for (const auto& bytes : bins)
        {
            if (bytes.empty())
            {
                break;
            }
            dense.push_back(bytes);
        }
        return dense;
    }

    static std::vector<std::vector<char>> expandDenseBins_(std::vector<std::vector<char>> dense,
                                                           const std::vector<std::vector<char>>& shape)
    {
        if (shape.size() >= dense.size())
        {
            auto result = shape;
            for (size_t i = 0; i < dense.size(); ++i)
            {
                result[i] = std::move(dense[i]);
            }
            for (size_t i = dense.size(); i < result.size(); ++i)
            {
                result[i].clear();
            }
            return result;
        }
        return dense;
    }

    static std::vector<std::vector<char>> applyContigDelta_(const std::vector<std::vector<char>>& parent_bins,
                                                            Action action, simdb::ValidValue<uint16_t> swap_index,
                                                            const std::vector<char>& payload)
    {
        auto dense = denseBins_(parent_bins);
        switch (action)
        {
        case Action::CARRY:
            break;
        case Action::CONTIG_CONTAINER_SWAP:
            assert(swap_index.isValid());
            dense[swap_index.getValue()] = payload;
            break;
        case Action::CONTIG_CONTAINER_ARRIVE:
            dense.push_back(payload);
            break;
        case Action::CONTIG_CONTAINER_DEPART:
            assert(!dense.empty());
            dense.erase(dense.begin());
            break;
        case Action::CONTIG_CONTAINER_BOOKENDS:
            assert(!dense.empty());
            dense.erase(dense.begin());
            dense.push_back(payload);
            break;
        default:
            throw DBException("Invalid contig delta action");
        }
        return expandDenseBins_(std::move(dense), parent_bins);
    }

    static std::vector<std::vector<char>> reconstituteContigBins_(const Checkpoint& checkpoint)
    {
        if (auto* snapshot = dynamic_cast<const ContigSnapshotCheckpoint*>(&checkpoint))
        {
            return snapshot->bins();
        }
        if (auto* delta = dynamic_cast<const ContigDeltaCheckpoint*>(&checkpoint))
        {
            const auto parent_bins = reconstituteContigBins_(*delta->parent_);
            return applyContigDelta_(parent_bins, delta->action_, delta->bin_index_, delta->payload_);
        }
        if (auto* vanished = dynamic_cast<const ScalarVanishedCheckpoint*>(&checkpoint))
        {
            return reconstituteContigBins_(*vanished->parent());
        }
        throw DBException("Cannot reconstitute contig bins from checkpoint");
    }
};

//! Per-contig-CID checkpoint chain builder.
class ContigCheckpointer : public CheckpointerBase
{
public:
    using CheckpointerBase::CheckpointerBase;

    std::shared_ptr<Checkpoint> createCheckpoint(const std::vector<std::vector<char>>& curr)
    {
        max_container_size_seen_ = std::max(max_container_size_seen_, static_cast<size_t>(countContigElements(curr)));

        const bool force_full = isLaggingTooMuch_();
        const auto classification = classifyContigChange(prev_contig_bins_, curr);

        if (!force_full && classification == ContigDeltaKind::CARRY)
        {
            return nullptr;
        }

        std::shared_ptr<Checkpoint> checkpoint;
        if (classification.kind == ContigDeltaKind::FULL || force_full)
        {
            checkpoint = std::make_shared<ContigSnapshotCheckpoint>(cid_, tip_, curr);
        } else
        {
            checkpoint = makeDeltaCheckpoint_(classification);
        }

        tip_ = checkpoint;
        prev_contig_bins_ = curr;
        return checkpoint;
    }

    size_t getMaxContainerSizeSeen() const { return max_container_size_seen_; }

private:
    static Action contigActionFromKind_(ContigDeltaKind kind)
    {
        switch (kind)
        {
        case ContigDeltaKind::CARRY:
            return Action::CARRY;
        case ContigDeltaKind::SWAP:
            return Action::CONTIG_CONTAINER_SWAP;
        case ContigDeltaKind::ARRIVE:
            return Action::CONTIG_CONTAINER_ARRIVE;
        case ContigDeltaKind::DEPART:
            return Action::CONTIG_CONTAINER_DEPART;
        case ContigDeltaKind::BOOKENDS:
            return Action::CONTIG_CONTAINER_BOOKENDS;
        case ContigDeltaKind::FULL:
            break;
        }
        throw DBException("Invalid contig delta kind");
    }

    std::shared_ptr<Checkpoint> makeDeltaCheckpoint_(const ContigDeltaClassification& classification)
    {
        return std::make_shared<ContigDeltaCheckpoint>(cid_, tip_, contigActionFromKind_(classification.kind),
                                                       classification.swap_index, classification.payload);
    }

    std::shared_ptr<Checkpoint> makeRootSnapshotAfterWireFull_(const CollectedData& /*full*/) override
    {
        return std::make_shared<ContigSnapshotCheckpoint>(cid_, nullptr, prev_contig_bins_);
    }

    std::shared_ptr<Checkpoint> makeReenabledSnapshot_() override
    {
        return std::make_shared<ContigSnapshotCheckpoint>(cid_, tip_, prev_contig_bins_);
    }

    std::vector<std::vector<char>> prev_contig_bins_;
    size_t max_container_size_seen_ = 0;
};

} // namespace simdb::argos
