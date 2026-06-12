// <CheckpointerCommon.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CollectedData.hpp"

#include <cassert>
#include <memory>

namespace simdb::argos {

class ScalarVanishedCheckpoint : public Checkpoint
{
public:
    enum class Kind { DISABLED, QUIETED };

    ScalarVanishedCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Kind kind) :
        cid_(cid),
        parent_(std::move(parent)),
        action_(kind == Kind::DISABLED ? Action::DISABLED : Action::QUIETED)
    {
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

} // namespace simdb::argos
