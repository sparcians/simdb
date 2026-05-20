#pragma once

#include <cstdint>

namespace simdb::argos {

enum class LifecycleAction : uint8_t { DISABLED = 0, ENABLED, QUIETED, AWAKENED, __FIRST_MINIFIER_ACTION };

inline constexpr uint8_t FULL_ACTION_FLAG = (uint8_t)LifecycleAction::__FIRST_MINIFIER_ACTION;

/// Human-readable name for byte-trace \c value output (matches enum member names).
inline const char* lifecycle_action_trace_value(const LifecycleAction action) noexcept
{
    switch (action)
    {
    case LifecycleAction::DISABLED:
        return "DISABLED";
    case LifecycleAction::ENABLED:
        return "ENABLED";
    case LifecycleAction::QUIETED:
        return "QUIETED";
    case LifecycleAction::AWAKENED:
        return "AWAKENED";
    case LifecycleAction::__FIRST_MINIFIER_ACTION:
        break;
    }
    return "?";
}

} // namespace simdb::argos
