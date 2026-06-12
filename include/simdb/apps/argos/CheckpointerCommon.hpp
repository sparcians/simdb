// <CheckpointerCommon.hpp> -*- C++ -*-

#pragma once

#include "simdb/apps/argos/CheckpointNodeBase.hpp"

#include <cassert>

namespace simdb::argos {

class ScalarVanishedCheckpoint : public ActionOnlyCheckpointBase
{
public:
    enum class Kind { DISABLED, QUIETED };

    ScalarVanishedCheckpoint(uint16_t cid, std::shared_ptr<Checkpoint> parent, Kind kind) :
        ActionOnlyCheckpointBase(cid, std::move(parent), kind == Kind::DISABLED ? Action::DISABLED : Action::QUIETED)
    {
        assert(parent_ != nullptr);
    }
};

} // namespace simdb::argos
