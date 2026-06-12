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

} // namespace simdb::argos

#include "simdb/apps/argos/CheckpointNodeBase.hpp"
#include "simdb/apps/argos/CheckpointerBase.hpp"
#include "simdb/apps/argos/CheckpointerCommon.hpp"
#include "simdb/apps/argos/CheckpointerContig.hpp"
#include "simdb/apps/argos/CheckpointerScalar.hpp"
#include "simdb/apps/argos/CheckpointerSparse.hpp"
