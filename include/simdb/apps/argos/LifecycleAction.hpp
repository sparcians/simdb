#pragma once

#include <cstdint>

namespace simdb::argos {

enum class LifecycleAction : uint8_t { DISABLED = 0, ENABLED, QUIETED, AWAKENED, __FIRST_MINIFIER_ACTION };

inline constexpr uint8_t FULL_ACTION_FLAG = (uint8_t)LifecycleAction::__FIRST_MINIFIER_ACTION;

} // namespace simdb::argos
