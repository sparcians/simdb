// <Checkpointer.hpp> -*- C++ -*-

#pragma once

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

    //! Bytes for this tick's wire record: [action][payload…] without leading CID
    //! (CollectedData prepends the CID in reset()).
    virtual std::unique_ptr<CollectedData> getMinifiedData() const = 0;

    //! Fully reconstituted value encoded as a FULL record (same framing as above).
    virtual std::unique_ptr<CollectedData> getFullData() const = 0;

    //! True when this node rebases the chain (Snapshot); false for Delta nodes.
    virtual bool isSnapshot() const = 0;

    //! Previous checkpoint in the chain, or nullptr for the first Snapshot.
    virtual std::shared_ptr<const Checkpoint> parent() const = 0;
};

class ScalarSnapshotCheckpoint : public Checkpoint
{
public:
    ScalarSnapshotCheckpoint(uint16_t cid, std::shared_ptr<const Checkpoint> parent, std::vector<char> payload) :
        cid_(cid),
        parent_(std::move(parent)),
        payload_(std::move(payload))
    {
    }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(Action::FULL);
        buf.append(payload_);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override { return getMinifiedData(); }

    bool isSnapshot() const override { return true; }

    std::shared_ptr<const Checkpoint> parent() const override { return parent_; }

private:
    uint16_t cid_;
    std::shared_ptr<const Checkpoint> parent_;
    std::vector<char> payload_;
};

class ScalarDeltaCheckpoint : public Checkpoint
{
public:
    ScalarDeltaCheckpoint(uint16_t cid, std::shared_ptr<const Checkpoint> parent) :
        cid_(cid),
        parent_(std::move(parent))
    {
    }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        data->getBuffer().append(Action::CARRY);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        assert(parent_ != nullptr);
        return parent_->getFullData();
    }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<const Checkpoint> parent() const override { return parent_; }

private:
    uint16_t cid_;
    std::shared_ptr<const Checkpoint> parent_;
};

//! Per-scalar-CID checkpoint chain builder.
class ScalarCheckpointer
{
public:
    explicit ScalarCheckpointer(uint16_t cid) :
        cid_(cid)
    {
    }

    uint16_t getCID() const { return cid_; }

    std::shared_ptr<const Checkpoint> tip() const { return tip_; }

    size_t getCyclesSinceLastFull() const { return cycles_since_last_full_; }

    //! Create the next checkpoint from raw scalar payload bytes (no framing).
    std::shared_ptr<const Checkpoint> createCheckpoint(const std::vector<char>& raw)
    {
        const auto kind = classifyScalarChange(last_scalar_bytes_, raw);
        std::shared_ptr<const Checkpoint> checkpoint;
        if (kind == ScalarDeltaKind::CHANGED)
        {
            checkpoint = std::make_shared<ScalarSnapshotCheckpoint>(cid_, tip_, raw);
            cycles_since_last_full_ = 0;
        } else
        {
            checkpoint = std::make_shared<ScalarDeltaCheckpoint>(cid_, tip_);
            ++cycles_since_last_full_;
        }

        tip_ = checkpoint;
        last_scalar_bytes_ = raw;
        return checkpoint;
    }

private:
    uint16_t cid_;
    std::shared_ptr<const Checkpoint> tip_;
    std::vector<char> last_scalar_bytes_;
    size_t cycles_since_last_full_ = 0;
};

} // namespace simdb::argos
