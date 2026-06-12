// <CheckpointerScalar.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointerBase.hpp"
#include "simdb/apps/argos/CheckpointDeltas.hpp"

#include <cassert>
#include <vector>

namespace simdb::argos {

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

    std::unique_ptr<CollectedData> getMinifiedData() const override { return getFullData(); }

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
        assert(parent_ != nullptr);
    }

    uint16_t getCID() const override { return cid_; }

    std::unique_ptr<CollectedData> getMinifiedData() const override
    {
        auto data = std::make_unique<CollectedData>(cid_);
        data->getBuffer().append(Action::CARRY);
        return data;
    }

    std::unique_ptr<CollectedData> getFullData() const override { return parent_->getFullData(); }

    bool isSnapshot() const override { return false; }

    std::shared_ptr<Checkpoint> parent() const override { return parent_; }

    Action getAction() const override { return Action::CARRY; }

    void detachFromParent() override { throw DBException("Cannot detach checkpoint - not a snapshot"); }

private:
    uint16_t cid_;
    std::shared_ptr<Checkpoint> parent_;
};

//! Per-scalar-CID checkpoint chain builder.
class ScalarCheckpointer : public CheckpointerBase
{
public:
    using CheckpointerBase::CheckpointerBase;

    std::shared_ptr<Checkpoint> createCheckpoint(const std::vector<char>& raw)
    {
        const auto kind = classifyScalarChange(last_scalar_bytes_, raw);
        const bool force_full = isHeartbeatBoundary_();

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

    std::shared_ptr<Checkpoint> makeCarryCheckpoint_() override
    {
        return std::make_shared<ScalarDeltaCheckpoint>(cid_, tip_);
    }

    std::shared_ptr<Checkpoint> makeRootSnapshotAfterWireFull_(const CollectedData& full) override
    {
        return std::make_shared<ScalarSnapshotCheckpoint>(cid_, nullptr, extractFullPayload_(full));
    }

    std::shared_ptr<Checkpoint> makeReenabledSnapshot_() override
    {
        return std::make_shared<ScalarSnapshotCheckpoint>(cid_, tip_, extractFullPayload_(*tip_->getFullData()));
    }

    std::vector<char> last_scalar_bytes_;
};

} // namespace simdb::argos
