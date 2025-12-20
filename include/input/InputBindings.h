#pragma once

#include <QMap>

#include "input/InputTypes.h"

namespace AIO::Input {

// Single source of truth for default input mappings.
//
// Design:
// - Physical inputs (keyboard keys, controller buttons, sticks) map into LogicalButton.
// - Apps consume LogicalButton state (polling or handlers) and decide what it means.
//   Example: LogicalButton::Confirm can mean "Select" in menus or "A" in GBA.
//
// Convention:
// - LogicalButton state is active-low (0 = pressed, 1 = released), matching GBA KEYINPUT.
struct InputBindings {
    struct ContextBindings {
        // Qt::Key -> LogicalButton
        QMap<int, LogicalButton> keyboard;

        // SDL_CONTROLLER_BUTTON_* (stored as int) -> LogicalButton
        QMap<int, LogicalButton> controllerButtons;
    };

    // Defaults for UI/navigation contexts.
    ContextBindings ui;

    // Defaults for emulator runtime contexts.
    ContextBindings emulator;

    struct StickConfig {
        // Matches existing behavior: press threshold higher than release threshold.
        int pressDeadzone = 20000;
        int releaseDeadzone = 16000;
        bool enableLeftStick = true;
        bool enableRightStick = true;
    } sticks;

    // Canonical Qt keys for synthesizing key events into widgets that still rely on
    // Qt keyPressEvent (e.g., web/streaming pages).
    // LogicalButton -> Qt::Key
    QMap<LogicalButton, int> canonicalQtKeys;
};

// Defaults are defined in one C++ file so editing is straightforward.
const InputBindings& DefaultInputBindings();

} // namespace AIO::Input
