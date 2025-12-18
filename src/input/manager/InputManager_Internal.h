#pragma once

// Internal helpers for InputManager implementation.
// This header is intentionally NOT installed under include/.

#include <cstdint>

#include "input/InputManager.h"

namespace AIO::Input {

inline uint32_t logicalMaskFor(LogicalButton button) {
    return 1u << static_cast<uint32_t>(button);
}

// Logical buttons primarily driven by controller polling each frame.
// These should not be allowed to latch from a previous frame.
inline constexpr uint32_t kControllerLogicalMask =
    (1u << static_cast<uint32_t>(LogicalButton::Confirm)) |
    (1u << static_cast<uint32_t>(LogicalButton::Back)) |
    (1u << static_cast<uint32_t>(LogicalButton::Aux1)) |
    (1u << static_cast<uint32_t>(LogicalButton::Aux2)) |
    (1u << static_cast<uint32_t>(LogicalButton::Select)) |
    (1u << static_cast<uint32_t>(LogicalButton::Start)) |
    (1u << static_cast<uint32_t>(LogicalButton::L)) |
    (1u << static_cast<uint32_t>(LogicalButton::R)) |
    (1u << static_cast<uint32_t>(LogicalButton::Home));

inline constexpr uint32_t kDirectionLogicalMask =
    (1u << static_cast<uint32_t>(LogicalButton::Up)) |
    (1u << static_cast<uint32_t>(LogicalButton::Down)) |
    (1u << static_cast<uint32_t>(LogicalButton::Left)) |
    (1u << static_cast<uint32_t>(LogicalButton::Right));

namespace detail {
extern bool gAioInputDebug;
ControllerFamily detectFamilyFromName(const QString& name);
void applyDefaultMappingsForFamily(InputManager* mgr, ControllerFamily fam);
} // namespace detail

} // namespace AIO::Input
