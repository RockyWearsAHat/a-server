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
        out.keyboard[Qt::Key_Return] = LogicalButton::Confirm;
        out.keyboard[Qt::Key_Enter] = LogicalButton::Confirm;
        out.keyboard[Qt::Key_Escape] = LogicalButton::Back;
        out.keyboard[Qt::Key_Home] = LogicalButton::Home;

        // Directions (arrows)
        out.keyboard[Qt::Key_Up] = LogicalButton::Up;
        out.keyboard[Qt::Key_Down] = LogicalButton::Down;
        out.keyboard[Qt::Key_Left] = LogicalButton::Left;
        out.keyboard[Qt::Key_Right] = LogicalButton::Right;

        // GBA-friendly defaults (still map into logical actions; the emulator decides meaning)
        out.keyboard[Qt::Key_Z] = LogicalButton::Confirm;
        out.keyboard[Qt::Key_X] = LogicalButton::Back;
        out.keyboard[Qt::Key_Backspace] = LogicalButton::Select;
        out.keyboard[Qt::Key_Shift] = LogicalButton::Select;
        out.keyboard[Qt::Key_Space] = LogicalButton::Start;
        out.keyboard[Qt::Key_Tab] = LogicalButton::Start;
        out.keyboard[Qt::Key_A] = LogicalButton::L;
        out.keyboard[Qt::Key_S] = LogicalButton::R;

        // -----------------------------
        // Controller (SDL button -> Logical)
        // -----------------------------
        // SDL's standard mapping already normalizes different controllers.
        // Treat "A" (bottom button) as Confirm and "B" (right button) as Back.
        out.controllerButtons[SDL_CONTROLLER_BUTTON_A] = LogicalButton::Confirm;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_B] = LogicalButton::Back;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_X] = LogicalButton::Aux1;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_Y] = LogicalButton::Aux2;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_BACK] = LogicalButton::Select;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_START] = LogicalButton::Start;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = LogicalButton::L;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = LogicalButton::R;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_GUIDE] = LogicalButton::Home;

        // D-pad directions are handled as a unified direction provider in InputManager,
        // but we also map the raw buttons for completeness.
        out.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_UP] = LogicalButton::Up;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = LogicalButton::Down;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = LogicalButton::Left;
        out.controllerButtons[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = LogicalButton::Right;

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
