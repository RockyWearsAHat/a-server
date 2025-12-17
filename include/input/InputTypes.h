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

// A single polled input frame for the whole program.
// This is global state (not per-app). Different systems (UI vs emulation)
// can consume different fields from the same snapshot.
struct InputSnapshot {
    // GBA KEYINPUT-style lower 10 bits (active-low).
    // Produced for the emulator core and also useful as a legacy view.
    uint16_t keyinput = 0x03FF;

    // Logical UI buttons (active-low bitfield indexed by LogicalButton).
    uint32_t logical = 0xFFFFFFFFu;

    // System buttons (non-GBA): e.g. bit0 = Home/Guide.
    uint32_t system = 0;
};

} // namespace AIO::Input
