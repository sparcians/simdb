// <CheckpointerBase.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointerCommon.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace simdb::argos {

//! Shared per-CID checkpoint chain state and lifecycle/refresh logic.
class CheckpointerBase
{
public:
    CheckpointerBase(uint16_t cid, size_t heartbeat) :
        cid_(cid),
        heartbeat_(heartbeat)
    {
        assert(heartbeat_ > 0);
    }

    virtual ~CheckpointerBase() = default;

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
        tip_ = makeCarryCheckpoint_();
    }

    void rebaseTipAfterWireFull(const CollectedData& full) { setNewTip(makeRootSnapshotAfterWireFull_(full)); }

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
        auto checkpoint = makeReenabledSnapshot_();
        tip_ = checkpoint;
        assert(getDistanceToSnapshot() == 0);
        return checkpoint;
    }

protected:
    bool isHeartbeatBoundary_() const { return (getDistanceToSnapshot() + 1) % heartbeat_ == 0; }

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

private:
    virtual std::shared_ptr<Checkpoint> makeCarryCheckpoint_() = 0;
    virtual std::shared_ptr<Checkpoint> makeRootSnapshotAfterWireFull_(const CollectedData& full) = 0;
    virtual std::shared_ptr<Checkpoint> makeReenabledSnapshot_() = 0;
};

} // namespace simdb::argos
