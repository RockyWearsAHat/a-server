#include "input/InputBindings.h"

#include <Qt>
#include <SDL2/SDL.h>

namespace AIO::Input {

const InputBindings& DefaultInputBindings() {
    // One big, organized struct defining default controls.
    // Edit this file to change defaults.
    static InputBindings b = [] {
        InputBindings out;

        // -----------------------------
        // Keyboard (Qt::Key -> Logical)
        // -----------------------------
        // UI / general
        out.ui.keyboard[Qt::Key_Return] = LogicalButton::Confirm;
        out.ui.keyboard[Qt::Key_Enter] = LogicalButton::Confirm;
        out.ui.keyboard[Qt::Key_Escape] = LogicalButton::Back;
        out.ui.keyboard[Qt::Key_Home] = LogicalButton::Home;

        // Directions (arrows)
        out.ui.keyboard[Qt::Key_Up] = LogicalButton::Up;
        out.ui.keyboard[Qt::Key_Down] = LogicalButton::Down;
        out.ui.keyboard[Qt::Key_Left] = LogicalButton::Left;
        out.ui.keyboard[Qt::Key_Right] = LogicalButton::Right;

        // Emulator defaults (GBA-friendly)
        out.emulator.keyboard[Qt::Key_Up] = LogicalButton::Up;
        out.emulator.keyboard[Qt::Key_Down] = LogicalButton::Down;
        out.emulator.keyboard[Qt::Key_Left] = LogicalButton::Left;
        out.emulator.keyboard[Qt::Key_Right] = LogicalButton::Right;

        out.emulator.keyboard[Qt::Key_Z] = LogicalButton::Confirm;
        out.emulator.keyboard[Qt::Key_X] = LogicalButton::Back;
        out.emulator.keyboard[Qt::Key_Shift] = LogicalButton::Select;
        out.emulator.keyboard[Qt::Key_Return] = LogicalButton::Start;
        out.emulator.keyboard[Qt::Key_Enter] = LogicalButton::Start;
        out.emulator.keyboard[Qt::Key_Space] = LogicalButton::Start;
        out.emulator.keyboard[Qt::Key_Tab] = LogicalButton::Start;
        out.emulator.keyboard[Qt::Key_A] = LogicalButton::L;
        out.emulator.keyboard[Qt::Key_S] = LogicalButton::R;

        // -----------------------------
        // Controller (SDL button -> Logical)
        // -----------------------------
        // SDL's standard mapping already normalizes different controllers.
        // Treat "A" (bottom button) as Confirm and "B" (right button) as Back.
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_A] = LogicalButton::Confirm;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_B] = LogicalButton::Back;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_X] = LogicalButton::Aux1;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_Y] = LogicalButton::Aux2;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_BACK] = LogicalButton::Select;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_START] = LogicalButton::Start;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = LogicalButton::L;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = LogicalButton::R;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_GUIDE] = LogicalButton::Home;

        // Emulator controller defaults mirror UI defaults (A=Confirm, B=Back, etc.)
        // so the same controller works consistently across the app.
        out.emulator.controllerButtons = out.ui.controllerButtons;

        // D-pad directions are handled as a unified direction provider in InputManager,
        // but we also map the raw buttons for completeness.
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_UP] = LogicalButton::Up;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = LogicalButton::Down;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = LogicalButton::Left;
        out.ui.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = LogicalButton::Right;

        out.emulator.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_UP] = LogicalButton::Up;
        out.emulator.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = LogicalButton::Down;
        out.emulator.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = LogicalButton::Left;
        out.emulator.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = LogicalButton::Right;

        // -----------------------------
        // Canonical Qt keys for synthetic events
        // -----------------------------
        out.canonicalQtKeys[LogicalButton::Confirm] = Qt::Key_Return;
        out.canonicalQtKeys[LogicalButton::Back] = Qt::Key_Escape;
        out.canonicalQtKeys[LogicalButton::Up] = Qt::Key_Up;
        out.canonicalQtKeys[LogicalButton::Down] = Qt::Key_Down;
        out.canonicalQtKeys[LogicalButton::Left] = Qt::Key_Left;
        out.canonicalQtKeys[LogicalButton::Right] = Qt::Key_Right;
        out.canonicalQtKeys[LogicalButton::Home] = Qt::Key_Home;

        return out;
    }();

    return b;
}

} // namespace AIO::Input
