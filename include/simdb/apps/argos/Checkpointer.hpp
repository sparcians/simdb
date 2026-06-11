// <Checkpointer.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"

#include <cstdint>
#include <memory>

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

} // namespace simdb::argos
