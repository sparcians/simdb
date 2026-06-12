// <Checkpointer.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"
#include "simdb/apps/argos/CollectedData.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace simdb::argos {

//! Encodings that C++ and python agree on. The python deserializers interpret
//! collected data using the header:
//!
//!   [uint16_t cid]     // collectable ID
//!   [uint8_t action]   // encoding
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

    // Specific to sparse containers
    SPARSE_CONTAINER_SWAP,
    SPARSE_CONTAINER_REMOVE
};

//! \class Checkpoint
//! \brief One node in a per-CID checkpoint chain (Snapshot or Delta).
//!
//! Checkpoints are immutable once created. PipelineStager and the waiting queue
//! hold shared_ptr's to the same nodes; reconstitution walks parent() links.
class Checkpoint
{
public:
    virtual ~Checkpoint() = default;

    //! Get the collectable ID associated with this checkpoint.
    virtual uint16_t getCID() const = 0;

    //! Bytes for this tick's wire record: [action][payload…] without leading CID
    //! (CollectedData prepends the CID in reset()).
    virtual std::unique_ptr<CollectedData> getMinifiedData() const = 0;

    //! Fully reconstituted value encoded as a FULL record (same framing as above).
    virtual std::unique_ptr<CollectedData> getFullData() const = 0;

    //! True when this node rebases the chain (Snapshot); false for Delta nodes.
    virtual bool isSnapshot() const = 0;

    //! Previous checkpoint in the chain, or nullptr for the first Snapshot.
    virtual std::shared_ptr<Checkpoint> parent() const = 0;

    //! Get the action associated with this checkpoint.
    virtual Action getAction() const = 0;

    //! Free up memory when the stager is done with our ancestor chain.
    virtual void detachFromParent() = 0;

    //! Count the number of hops to our last snapshot.
    size_t getDistanceToSnapshot() const
    {
        size_t len = 0;
        auto chkpt = this;
        while (chkpt)
        {
            if (chkpt->isSnapshot())
            {
                break;
            }
            chkpt = chkpt->parent().get();
            ++len;
        }
        return len;
    }
};

class ScalarSnapshotCheckpoint : public Checkpoint
{
public:
    ScalarSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, std::vector<char> payload) :
        cid_(cid),
        parent_(std::move(parent)),
        payload_(std::move(payload))
    {
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        // Snapshots only return FULL
        return getFullData();
    }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(Action::FULL);
        buf.append(payload_);
        return data;
    }

    bool isSnapshot() const override { return true; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return Action::FULL; }

    void detachFromParent() override { parent_.reset(); }

private:
    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    std::vector<char> payload_;
};

class ScalarDeltaCheckpoint : public Checkpoint
{
public:
    ScalarDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent) :
        cid_(cid),
        parent_(std::move(parent))
    {
        // Deltas cannot exist by themselves
        assert(parent_ != nullptr);
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        // Scalars only have CARRY as their lone minification algo
        auto data = std::make_unique<CollectedData>(cid_);
        data->getBuffer().append(Action::CARRY);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        // Since deltas are always CARRY, defer to the parent
        // to get the FULL data. Does not matter if the parent
        // is a delta or a snapshot; we keep going backwards
        // until a snapshot checkpoint is hit.
        return parent_->getFullData();
    }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return Action::CARRY; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

private:
    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
};

class ScalarVanishedCheckpoint : public Checkpoint
{
public:
    enum class Kind { DISABLED, QUIETED };

    ScalarVanishedCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Kind kind) :
        cid_(cid),
        parent_(std::move(parent)),
        action_(kind == Kind::DISABLED ? Action::DISABLED : Action::QUIETED)
    {
        // We should never get here as our first checkpoint
        assert(parent_ != nullptr);
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        data->getBuffer().append(action_);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override { return parent_->getFullData(); }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return action_; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

private:
    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    Action action_;
};

//! Per-scalar-CID checkpoint chain builder.
class ScalarCheckpointer
{
public:
    ScalarCheckpointer(uint16_t cid, size_t heartbeat) :
        cid_(cid),
        heartbeat_(heartbeat)
    {
        assert(heartbeat_ > 0);
    }

    uint16_t getCID() const { return cid_; }

    size_t getHeartbeat() const { return heartbeat_; }

    std::shared_ptr<Checkpoint> tip() const { return tip_; }

    void setNewTip(std::shared_ptr<Checkpoint> tip)
    {
        assert(tip != nullptr);
        assert(tip->isSnapshot());
        assert(tip->getDistanceToSnapshot() == 0);
        tip_ = tip;
    }

    size_t getDistanceToSnapshot() const
    {
        if (!tip_)
        {
            return heartbeat_ - 1;
        }
        return tip_->getDistanceToSnapshot();
    }

    bool isRefreshable() const
    {
        if (!tip_)
        {
            return false;
        }
        const auto action = tip_->getAction();
        return action != Action::DISABLED && action != Action::QUIETED;
    }

    bool isDueForWireRefresh() const { return isRefreshable() && (getDistanceToSnapshot() + 1 >= heartbeat_); }

    void recordMissedFlush()
    {
        assert(isRefreshable());
        tip_ = std::make_shared<ScalarDeltaCheckpoint>(cid_, tip_);
    }

    void rebaseTipAfterWireFull(const CollectedData& full)
    {
        setNewTip(std::make_shared<ScalarSnapshotCheckpoint>(cid_, nullptr, extractFullPayload_(full)));
    }

    std::shared_ptr<Checkpoint> createCheckpoint(const std::vector<char>& raw)
    {
        const auto kind = classifyScalarChange(last_scalar_bytes_, raw);
        const bool force_full = (getDistanceToSnapshot() + 1) % heartbeat_ == 0;

        std::shared_ptr<Checkpoint> checkpoint;
        if (kind == ScalarDeltaKind::CHANGED || force_full)
        {
            checkpoint = std::make_shared<ScalarSnapshotCheckpoint>(cid_, tip_, raw);
        } else
        {
            checkpoint = std::make_shared<ScalarDeltaCheckpoint>(cid_, tip_);
        }

        tip_ = checkpoint;
        last_scalar_bytes_ = raw;
        return checkpoint;
    }

    std::shared_ptr<Checkpoint> createDisabledCheckpoint()
    {
        return appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind::DISABLED);
    }

    std::shared_ptr<Checkpoint> createQuietedCheckpoint()
    {
        return appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind::QUIETED);
    }

    std::shared_ptr<Checkpoint> createReenabledCheckpoint()
    {
        assert(tip_ != nullptr);
        const auto payload = extractFullPayload_(*tip_->getFullData());
        auto checkpoint = std::make_shared<ScalarSnapshotCheckpoint>(cid_, tip_, payload);
        tip_ = checkpoint;
        assert(getDistanceToSnapshot() == 0);
        return checkpoint;
    }

private:
    static std::vector<char> extractFullPayload_(const CollectedData& full)
    {
        static constexpr auto kHeaderBytes = sizeof(uint16_t) + sizeof(uint8_t);
        const auto& bytes = full.getData();
        if (bytes.size() <= kHeaderBytes)
        {
            return {};
        }
        return std::vector<char>(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes), bytes.end());
    }

    std::shared_ptr<Checkpoint> appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind kind)
    {
        assert(tip_ != nullptr);
        auto checkpoint = std::make_shared<ScalarVanishedCheckpoint>(cid_, tip_, kind);
        tip_ = checkpoint;
        return checkpoint;
    }

    uint16_t cid_;
    size_t heartbeat_;
    std::shared_ptr<Checkpoint> tip_;
    std::vector<char> last_scalar_bytes_;
};

class ContigSnapshotCheckpoint : public Checkpoint
{
public:
    ContigSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, std::vector<std::vector<char>> bins) :
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
        buf.append(encodeContigFullTail_(bins_));
        return data;
    }

    bool isSnapshot() const override { return true; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return Action::FULL; }

    void detachFromParent() override { parent_.reset(); }

    const std::vector<std::vector<char>>& bins() const { return bins_; }

private:
    static std::vector<char> encodeContigFullTail_(const std::vector<std::vector<char>>& bins)
    {
        std::vector<char> tail;
        StreamBuffer buf(tail);

        const auto size = countContigElements(bins);
        buf.append(size);
        for (uint16_t i = 0; i < size; ++i)
        {
            buf.append(bins[i]);
        }
        return tail;
    }

    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    std::vector<std::vector<char>> bins_;
};

class ContigDeltaCheckpoint : public Checkpoint
{
public:
    ContigDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action,
                          const simdb::ValidValue<uint16_t>& swap_index, std::vector<char> payload) :
        cid_(cid),
        parent_(std::move(parent)),
        action_(action),
        payload_(std::move(payload))
    {
        assert(parent_ != nullptr);
        if (swap_index.isValid())
        {
            swap_index_ = swap_index.getValue();
        }
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(action_);
        if (action_ == Action::CONTIG_CONTAINER_SWAP)
        {
            assert(swap_index_.isValid());
            buf.append(swap_index_.getValue());
        }
        if (!payload_.empty())
        {
            buf.append(payload_);
        }
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        const auto parent_bins = reconstituteContigBins_(*parent_);
        const auto full_bins = applyContigDelta_(parent_bins, action_, swap_index_, payload_);
        auto snapshot = ContigSnapshotCheckpoint(cid_, nullptr, full_bins);
        return snapshot.getFullData();
    }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return action_; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

private:
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
            return applyContigDelta_(parent_bins, delta->action_, delta->swap_index_, delta->payload_);
        }
        if (auto* vanished = dynamic_cast<const ScalarVanishedCheckpoint*>(&checkpoint))
        {
            return reconstituteContigBins_(*vanished->parent());
        }
        throw DBException("Cannot reconstitute contig bins from checkpoint");
    }

    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
    Action action_;
    simdb::ValidValue<uint16_t> swap_index_;
    std::vector<char> payload_;
};

//! Per-contig-CID checkpoint chain builder.
class ContigCheckpointer
{
public:
    ContigCheckpointer(uint16_t cid, size_t heartbeat) :
        cid_(cid),
        heartbeat_(heartbeat)
    {
        assert(heartbeat_ > 0);
    }

    uint16_t getCID() const { return cid_; }

    size_t getHeartbeat() const { return heartbeat_; }

    std::shared_ptr<Checkpoint> tip() const { return tip_; }

    void setNewTip(std::shared_ptr<Checkpoint> tip)
    {
        assert(tip != nullptr);
        assert(tip->isSnapshot());
        assert(tip->getDistanceToSnapshot() == 0);
        tip_ = tip;
    }

    size_t getDistanceToSnapshot() const
    {
        if (!tip_)
        {
            return heartbeat_ - 1;
        }
        return tip_->getDistanceToSnapshot();
    }

    bool isRefreshable() const
    {
        if (!tip_)
        {
            return false;
        }
        const auto action = tip_->getAction();
        return action != Action::DISABLED && action != Action::QUIETED;
    }

    bool isDueForWireRefresh() const { return isRefreshable() && (getDistanceToSnapshot() + 1 >= heartbeat_); }

    void recordMissedFlush()
    {
        assert(isRefreshable());
        tip_ = std::make_shared<ContigDeltaCheckpoint>(cid_, tip_, Action::CARRY, simdb::ValidValue<uint16_t>{},
                                                       std::vector<char>{});
    }

    void rebaseTipAfterWireFull(const CollectedData& /*full*/)
    {
        setNewTip(std::make_shared<ContigSnapshotCheckpoint>(cid_, nullptr, prev_contig_bins_));
    }

    std::shared_ptr<Checkpoint> createCheckpoint(const std::vector<std::vector<char>>& curr)
    {
        const bool force_full = (getDistanceToSnapshot() + 1) % heartbeat_ == 0;
        const auto classification = classifyContigChange(prev_contig_bins_, curr);

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

    std::shared_ptr<Checkpoint> createDisabledCheckpoint()
    {
        return appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind::DISABLED);
    }

    std::shared_ptr<Checkpoint> createQuietedCheckpoint()
    {
        return appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind::QUIETED);
    }

    std::shared_ptr<Checkpoint> createReenabledCheckpoint()
    {
        assert(tip_ != nullptr);
        auto checkpoint = std::make_shared<ContigSnapshotCheckpoint>(cid_, tip_, prev_contig_bins_);
        tip_ = checkpoint;
        assert(getDistanceToSnapshot() == 0);
        return checkpoint;
    }

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

    std::shared_ptr<Checkpoint> appendLifecycleCheckpoint_(ScalarVanishedCheckpoint::Kind kind)
    {
        assert(tip_ != nullptr);
        auto checkpoint = std::make_shared<ScalarVanishedCheckpoint>(cid_, tip_, kind);
        tip_ = checkpoint;
        return checkpoint;
    }

    uint16_t cid_;
    size_t heartbeat_;
    std::shared_ptr<Checkpoint> tip_;
    std::vector<std::vector<char>> prev_contig_bins_;
};

} // namespace simdb::argos
