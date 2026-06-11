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

    uint16_t getCID() const { return cid_; }

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

    uint16_t getCID() const { return cid_; }

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

    uint16_t getCID() const { return cid_; }

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

} // namespace simdb::argos
