// <Checkpoint.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CollectedData.hpp"

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

//! \class CollectableCheckpoint
//! \brief Base class for all collectable checkpoints.
class CollectableCheckpoint
{
public:
    virtual ~CollectableCheckpoint() = default;

    CollectableCheckpoint* getPrev() const { return prev_.get(); }

    CollectableCheckpoint* getNext() const { return next_; }

    uint64_t getWindowID() const { return window_id_; }

protected:
    CollectableCheckpoint(std::shared_ptr<CollectableCheckpoint> prev, uint64_t window_id)
        : prev_(prev)
        , window_id_(window_id)
    {
        if (prev_)
        {
            prev_->next_ = this;
        }
    }

    std::shared_ptr<CollectableCheckpoint> prev_;
    CollectableCheckpoint* next_ = nullptr;
    const uint64_t window_id_;
};

class ScalarCheckpoint final : public CollectableCheckpoint
{
public:
    // Use this ctor for data collection events
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, const std::vector<char>& full_bytes)
        : CollectableCheckpoint(prev, window_id)
        , full_bytes_(full_bytes)
    {}

    // Use this ctor for enable/disabled events
    ScalarCheckpoint(std::shared_ptr<ScalarCheckpoint> prev, uint64_t window_id, bool switched_to_enabled)
        : CollectableCheckpoint(prev, window_id)
        , enabled_change_(switched_to_enabled)
    {}

    ScalarCheckpoint* prev()
    {
        return static_cast<ScalarCheckpoint*>(getPrev());
    }

    ScalarCheckpoint* next()
    {
        return static_cast<ScalarCheckpoint*>(getNext());
    }

    std::unique_ptr<CollectedData> encodeForPipeline(uint16_t cid, bool force_snapshot)
    {
        // ScalarCheckpoints only return:
        //   ENABLED
        //   DISABLED
        //   FULL
        //   CARRY
        //
        // The incoming force_snapshot flag is true when we require a heartbeat refresh.
        if (enabled_change_.isValid())
        {
            return encodeEnabledEvt_(cid);
        }

        if (force_snapshot)
        {
            return encodeFull_(cid);
        }

        if (!prev())
        {
            return encodeFull_(cid);
        }

        if (prev()->full_bytes_ != full_bytes_)
        {
            return encodeFull_(cid);
        }

        return encodeDelta_(cid);
    }

private:
    std::unique_ptr<CollectedData> encodeEnabledEvt_(uint16_t cid)
    {
        if (enabled_change_)
        {
            // Get the full bytes from the checkpoint chain
            auto anchor = prev();
            while (anchor)
            {
                if (!anchor->full_bytes_.empty())
                {
                    break;
                }
                anchor = anchor->prev();
            }

            auto valid = anchor && !anchor->full_bytes_.empty();
            if (!valid)
            {
                return nullptr;
            }

            auto encoded = std::make_unique<CollectedData>(cid);
            auto& buf = encoded->getBuffer();
            buf.append(Action::ENABLED);
            buf.append(anchor->full_bytes_);
            return encoded;
        }

        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::DISABLED);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeFull_(uint16_t cid)
    {
        assert(!full_bytes_.empty());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::FULL);
        buf.append(full_bytes_);
        return encoded;
    }

    std::unique_ptr<CollectedData> encodeDelta_(uint16_t cid)
    {
        assert(prev() && prev()->full_bytes_ == full_bytes_ && !full_bytes_.empty());
        auto encoded = std::make_unique<CollectedData>(cid);
        auto& buf = encoded->getBuffer();
        buf.append(Action::CARRY);
        return encoded;
    }

    std::vector<char> full_bytes_;
    ValidValue<bool> enabled_change_;
};

class ContigContainerCheckpoint : public CollectableCheckpoint
{
public:
    // Use this ctor for data collection events
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id, const std::vector<std::vector<char>>& full_bytes)
        : CollectableCheckpoint(prev, window_id)
        , full_bytes_(full_bytes)
    {}

    // Use this ctor for enable/disabled events
    ContigContainerCheckpoint(std::shared_ptr<ContigContainerCheckpoint> prev, uint64_t window_id, bool switched_to_enabled)
        : CollectableCheckpoint(prev, window_id)
        , enabled_change_(switched_to_enabled)
    {}

    ContigContainerCheckpoint* prev()
    {
        return static_cast<ContigContainerCheckpoint*>(getPrev());
    }

    ContigContainerCheckpoint* next()
    {
        return static_cast<ContigContainerCheckpoint*>(getNext());
    }

private:
    std::vector<std::vector<char>> full_bytes_;
    ValidValue<bool> enabled_change_;
};

class SparseContainerCheckpoint : public CollectableCheckpoint
{
public:
    // Use this ctor for data collection events
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id, const std::map<uint16_t, std::vector<char>>& full_bytes)
        : CollectableCheckpoint(prev, window_id)
        , full_bytes_(full_bytes)
    {}

    // Use this ctor for enable/disabled events
    SparseContainerCheckpoint(std::shared_ptr<SparseContainerCheckpoint> prev, uint64_t window_id, bool switched_to_enabled)
        : CollectableCheckpoint(prev, window_id)
        , enabled_change_(switched_to_enabled)
    {}

    SparseContainerCheckpoint* prev()
    {
        return static_cast<SparseContainerCheckpoint*>(getPrev());
    }

    SparseContainerCheckpoint* next()
    {
        return static_cast<SparseContainerCheckpoint*>(getNext());
    }

private:
    std::map<uint16_t, std::vector<char>> full_bytes_;
    ValidValue<bool> enabled_change_;
};

} // namespace simdb::argos
