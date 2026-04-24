#pragma once

#include <cstdint>

namespace simdb::collection {

enum class LifecycleAction : uint8_t
{
    DISABLED = 0,
    ENABLED,
    QUIETED,
    AWAKENED,
    __FIRST_MINIFIER_ACTION
};

} // namespace simdb::collection
