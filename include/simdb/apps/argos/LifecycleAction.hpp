#pragma once

#include <cstdint>

namespace simdb::collection {

enum class LifecycleAction : uint16_t
{
    DISABLED = 0,
    ENABLED,
    QUIETD,
    AWAKENED,
    __FIRST_MINIFIER_ACTION
};

} // namespace simdb::collection
