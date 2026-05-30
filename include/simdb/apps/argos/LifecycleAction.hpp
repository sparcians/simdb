#pragma once

#include <cstdint>

namespace simdb::argos {

//! Each Minifier class has its own enum that encodes "which optimization did I use
//! before sending the collected bytes to the PipelineStager". The Argos python code
//! understands all of these Minifier optimization enums and can deserialize the bytes.
//! The LifecycleAction enum contains everything that is common to all Minifiers.
enum class LifecycleAction : uint8_t { DISABLED = 0, ENABLED, QUIETED, AWAKENED, __FIRST_MINIFIER_ACTION };

//! Every Minifier-specific enum will start with this int value.
inline constexpr uint8_t FULL_ACTION_FLAG = (uint8_t)LifecycleAction::__FIRST_MINIFIER_ACTION;

} // namespace simdb::argos
