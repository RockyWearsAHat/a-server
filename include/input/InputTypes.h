#pragma once

namespace AIO::Input {

// Shared input types that are safe to include everywhere without pulling in
// the full InputManager definition.

enum class LogicalButton {
    Confirm,
    Back,
    Aux1,
    Aux2,
    Start,
    Select,
    L,
    R,
    Up,
    Down,
    Left,
    Right,
    Home,
};

enum class ControllerFamily {
    Unknown,
    Xbox,
    PlayStation,
    Nintendo,
    Generic,
};

} // namespace AIO::Input
