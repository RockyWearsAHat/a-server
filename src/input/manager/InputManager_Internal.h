#pragma once

// Internal helpers for InputManager implementation.
// This header is intentionally NOT installed under include/.

#include <cstdint>

#include "input/InputTypes.h"

namespace AIO::Input {

inline uint32_t logicalMaskFor(LogicalButton button) {
    return 1u << static_cast<uint32_t>(button);
}

namespace detail {
extern bool gAioInputDebug;
} // namespace detail

} // namespace AIO::Input
