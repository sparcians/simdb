// <CheckpointNodeBase.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"
#include "simdb/apps/argos/CollectedData.hpp"
#include "simdb/utils/ValidValue.hpp"

#include <cassert>
#include <memory>
#include <vector>

namespace simdb::argos {

class CheckpointNodeBase : public Checkpoint
{
public:
    ~CheckpointNodeBase() override = default;

    uint16_t getCID() const override { return cid_; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

protected:
    CheckpointNodeBase(uint16_t cid, std::shared_ptr<Checkpoint> parent) :
        cid_(cid),
        parent_(std::move(parent))
    {
    }

    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
};

class SnapshotCheckpointBase : public CheckpointNodeBase
{
public:
    ~SnapshotCheckpointBase() override = default;

    std::unique_ptr<CollectedData> getMinifiedData() const override { return getFullData(); }

    std::unique_ptr<CollectedData> getFullData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(Action::FULL);
        appendFullTail_(buf);
        return data;
    }

    bool isSnapshot() const override { return true; }

    Action getAction() const override { return Action::FULL; }

    void detachFromParent() override { parent_.reset(); }

protected:
    SnapshotCheckpointBase(uint16_t cid, std::shared_ptr<Checkpoint> parent) :
        CheckpointNodeBase(cid, std::move(parent))
    {
    }

private:
    virtual void appendFullTail_(StreamBuffer& buf) const = 0;
};

class ActionOnlyCheckpointBase : public CheckpointNodeBase
{
public:
    ~ActionOnlyCheckpointBase() override = default;

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        data->getBuffer().append(action_);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override { return parent_->getFullData(); }

    bool isSnapshot() const override { return false; }

    Action getAction() const override { return action_; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

protected:
    ActionOnlyCheckpointBase(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action) :
        CheckpointNodeBase(cid, std::move(parent)),
        action_(action)
    {
    }

    Action action_;
};

class IndexedDeltaCheckpointBase : public CheckpointNodeBase
{
public:
    ~IndexedDeltaCheckpointBase() override = default;

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        auto& buf = data->getBuffer();
        buf.append(action_);
        appendMinifiedTail_(buf);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override { return makeFullData_(); }

    bool isSnapshot() const override { return false; }

    Action getAction() const override { return action_; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

protected:
    IndexedDeltaCheckpointBase(uint16_t cid, std::shared_ptr<Checkpoint> parent, Action action,
                               const simdb::ValidValue<uint16_t>& bin_index, std::vector<char> payload) :
        CheckpointNodeBase(cid, std::move(parent)),
        action_(action),
        payload_(std::move(payload))
    {
        assert(parent_ != nullptr);
        if (bin_index.isValid())
        {
            bin_index_ = bin_index.getValue();
        }
    }

    Action action_;
    simdb::ValidValue<uint16_t> bin_index_;
    std::vector<char> payload_;

private:
    virtual void appendMinifiedTail_(StreamBuffer& buf) const = 0;
    virtual std::unique_ptr<CollectedData> makeFullData_() const = 0;
};

} // namespace simdb::argos
