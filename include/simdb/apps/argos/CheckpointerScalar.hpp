// <CheckpointerScalar.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointDeltas.hpp"
#include "simdb/apps/argos/CheckpointNodeBase.hpp"
#include "simdb/apps/argos/CheckpointerBase.hpp"

#include <cassert>
#include <vector>

namespace simdb::argos {

class ScalarSnapshotCheckpoint : public SnapshotCheckpointBase
{
public:
    ScalarSnapshotCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, std::vector<char> payload) :
        SnapshotCheckpointBase(cid, std::move(parent)),
        payload_(std::move(payload))
    {
    }

private:
    void appendFullTail_(StreamBuffer& buf) const override { buf.append(payload_); }

    std::vector<char> payload_;
};

class ScalarDeltaCheckpoint : public ActionOnlyCheckpointBase
{
public:
    ScalarDeltaCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent) :
        ActionOnlyCheckpointBase(cid, std::move(parent), Action::CARRY)
    {
        assert(parent_ != nullptr);
    }
};

//! Per-scalar-CID checkpoint chain builder.
class ScalarCheckpointer : public CheckpointerBase
{
public:
    using CheckpointerBase::CheckpointerBase;

    std::shared_ptr<Checkpoint> createCheckpoint(const std::vector<char>& raw)
    {
        const auto classification = classifyScalarChange(last_scalar_bytes_, raw);
        const bool force_full = isLaggingTooMuch_();

        if (!force_full && classification == ScalarDeltaKind::UNCHANGED)
        {
            return nullptr;
        }

        std::shared_ptr<Checkpoint> checkpoint;
        if (classification == ScalarDeltaKind::CHANGED || force_full)
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

    std::shared_ptr<Checkpoint> makeReenabledSnapshot_() override
    {
        return std::make_shared<ScalarSnapshotCheckpoint>(cid_, tip_, extractFullPayload_(*tip_->getFullData()));
    }

    std::vector<char> last_scalar_bytes_;
};

} // namespace simdb::argos
