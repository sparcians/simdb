// <CheckpointDeltas.hpp> -*- C++ -*-

#pragma once

#include <iostream>
#include <vector>

namespace simdb::argos {

//! Result of comparing two consecutive scalar payloads (raw bytes, no framing).
enum class ScalarDeltaKind { Unchanged, Changed };

//! Compare previous and current scalar payloads.
//!
//! Returns Changed when \p prev is empty (first collection has no baseline).
//! Scalars only support CARRY when Unchanged; Changed implies a FULL snapshot.
inline ScalarDeltaKind classifyScalarChange(const std::vector<char>& prev, const std::vector<char>& curr)
{
    if (prev.empty())
    {
        return ScalarDeltaKind::Changed;
    }

    return prev == curr ? ScalarDeltaKind::Unchanged : ScalarDeltaKind::Changed;
}

inline std::ostream& operator<<(std::ostream& os, ScalarDeltaKind kind)
{
    switch (kind)
    {
    case ScalarDeltaKind::Unchanged:
        return os << "Unchanged";
    case ScalarDeltaKind::Changed:
        return os << "Changed";
    }
    return os << "InvalidScalarDeltaKind";
}

} // namespace simdb::argos
