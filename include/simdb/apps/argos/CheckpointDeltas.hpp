// <CheckpointDeltas.hpp> -*- C++ -*-

#pragma once

#include "simdb/Exceptions.hpp"

#include <iostream>
#include <vector>

namespace simdb::argos {

//! Result of comparing two consecutive scalar payloads (raw bytes, no framing).
enum class ScalarDeltaKind { UNCHANGED, CHANGED };

//! Compare previous and current scalar payloads.
//!
//! Returns CHANGED when \p prev is empty (first collection has no baseline).
//! Scalars only support CARRY when UNCHANGED; CHANGED implies a FULL snapshot.
inline ScalarDeltaKind classifyScalarChange(const std::vector<char>& prev, const std::vector<char>& curr)
{
    if (prev.empty())
    {
        return ScalarDeltaKind::CHANGED;
    }

    return prev == curr ? ScalarDeltaKind::UNCHANGED : ScalarDeltaKind::CHANGED;
}

inline std::ostream& operator<<(std::ostream& os, ScalarDeltaKind kind)
{
    switch (kind)
    {
    case ScalarDeltaKind::UNCHANGED:
        return os << "UNCHANGED";
    case ScalarDeltaKind::CHANGED:
        return os << "CHANGED";
    }
    throw DBException("Invalid ScalarDeltaKind");
}

} // namespace simdb::argos
