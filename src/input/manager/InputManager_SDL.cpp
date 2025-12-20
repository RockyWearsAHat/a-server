#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QString>

#include <algorithm>
#include <cmath>

namespace AIO::Input {

namespace detail {
bool gAioInputDebug = false;
} // namespace detail

namespace {

enum class Dir { None, Up, Down, Left, Right };
enum class DirSource { None, Dpad, Stick };

Dir collapseToSingle(bool up, bool down, bool left, bool right) {
    if (up && down) {
        up = false;
        down = false;
    }
    if (left && right) {
        left = false;
        right = false;
    }

    // Collapse diagonals to a single axis. Prefer vertical (menus feel better).
    if ((up || down) && (left || right)) {
        left = false;
        right = false;
    }

    if (up) return Dir::Up;
    if (down) return Dir::Down;
    if (left) return Dir::Left;
    if (right) return Dir::Right;
    return Dir::None;
}

const char* dirName(Dir d) {
    switch (d) {
        case Dir::Up: return "Up";
        case Dir::Down: return "Down";
        case Dir::Left: return "Left";
        case Dir::Right: return "Right";
        default: return "None";
    }
}

const char* sourceName(DirSource s) {
    switch (s) {
        case DirSource::Dpad: return "Dpad";
        case DirSource::Stick: return "Stick";
        default: return "None";
    }
}

} // namespace

void InputManager::pollSdl() {
    // Capture previous logical state for edge detection.
    lastLogicalButtonsDown_ = logicalButtonsDown_;

    // Drain SDL events; we read current state via SDL_GameControllerGet* below.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        // Intentionally consume controller events.
    }

    SDL_GameControllerUpdate();

    // Check for controller hotplug.
    static int lastNumJoysticks = -1;
    const int numJoysticks = SDL_NumJoysticks();
    if (numJoysticks != lastNumJoysticks) {
        for (auto c : controllers) {
            SDL_GameControllerClose(c);
        }
        controllers.clear();

        for (int i = 0; i < numJoysticks; ++i) {
            if (!SDL_IsGameController(i)) continue;

            SDL_GameController* pad = SDL_GameControllerOpen(i);
            if (!pad) continue;

            controllers.insert(i, pad);
            const QString name = QString::fromUtf8(SDL_GameControllerName(pad));
            qDebug() << "Opened Gamepad:" << name;
        }

        lastNumJoysticks = numJoysticks;
    }

    // System buttons (Guide/Home/PS) are tracked separately from emulation input.
    systemButtonsDown_ = 0;

    // Controller-derived logical state is recomputed every frame (no latching).
    uint32_t controllerLogical = 0xFFFFFFFFu;

    // Unified direction provider state.
    static DirSource lastSource = DirSource::None;
    static qint64 lastSourceMs = 0;
    static QElapsedTimer sourceTimer;
    if (!sourceTimer.isValid()) sourceTimer.start();

    static Dir lastDpadDir = Dir::None;
    static Dir lastStickDir = Dir::None;
    static Dir lastChosenDir = Dir::None;
    static DirSource lastLoggedSource = DirSource::None;

    for (auto pad : controllers) {
        if (detail::gAioInputDebug) {
            const int du = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
            const int dd = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            const int dl = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            const int dr = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

            const int lx = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
            const int ly = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
            const int rx = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
            const int ry = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

            qDebug() << "[INPUT] dpad" << du << dd << dl << dr << "axes" << lx << ly << rx << ry;
        }

        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE)) {
            systemButtonsDown_ |= 0x1u; // Home/Guide
        }

        // Non-direction logical buttons from mapping.
        for (auto it = bindings_.controllerButtons.begin(); it != bindings_.controllerButtons.end(); ++it) {
            const int sdlBtn = it.key();
            const LogicalButton logical = it.value();
            if (SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(sdlBtn))) {
                controllerLogical &= ~logicalMaskFor(logical);
            }
        }

        // Direction intent: D-pad
        const bool dpadUp = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        const bool dpadDown = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        const bool dpadLeft = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        const bool dpadRight = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        const Dir dpadDir = collapseToSingle(dpadUp, dpadDown, dpadLeft, dpadRight);

        // Direction intent: sticks
        const int pressDeadzone = bindings_.sticks.pressDeadzone;
        const int releaseDeadzone = bindings_.sticks.releaseDeadzone;

        const int lx = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
        const int ly = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
        const int rx = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
        const int ry = static_cast<int>(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

        auto axisToDir = [&](int x, int y) -> Dir {
            const int ax = std::abs(x);
            const int ay = std::abs(y);
            const bool wantHorizontal = ax > ay;

            bool up = false;
            bool down = false;
            bool left = false;
            bool right = false;

            if (wantHorizontal) {
                if (x <= -pressDeadzone) left = true;
                if (x >= pressDeadzone) right = true;
            } else {
                if (y <= -pressDeadzone) up = true;
                if (y >= pressDeadzone) down = true;
            }

            if (ax < releaseDeadzone && ay < releaseDeadzone) {
                return Dir::None;
            }

            return collapseToSingle(up, down, left, right);
        };

        const Dir stickDir = [&]() {
            Dir l = Dir::None;
            Dir r = Dir::None;
            if (bindings_.sticks.enableLeftStick) {
                l = axisToDir(lx, ly);
            }
            if (bindings_.sticks.enableRightStick) {
                r = axisToDir(rx, ry);
            }

            if (l == Dir::None) return r;
            if (r == Dir::None) return l;

            const int lmag = std::max(std::abs(lx), std::abs(ly));
            const int rmag = std::max(std::abs(rx), std::abs(ry));
            return (rmag > lmag) ? r : l;
        }();

        const qint64 nowMs = sourceTimer.elapsed();
        if (dpadDir != lastDpadDir) {
            lastDpadDir = dpadDir;
            if (dpadDir != Dir::None) {
                lastSource = DirSource::Dpad;
                lastSourceMs = nowMs;
            }
        }
        if (stickDir != lastStickDir) {
            lastStickDir = stickDir;
            if (stickDir != Dir::None) {
                lastSource = DirSource::Stick;
                lastSourceMs = nowMs;
            }
        }

        Dir chosen = Dir::None;
        if (stickDir != Dir::None && dpadDir != Dir::None) {
            chosen = (lastSource == DirSource::Stick) ? stickDir : dpadDir;
        } else if (stickDir != Dir::None) {
            chosen = stickDir;
        } else {
            chosen = dpadDir;
        }

        if (detail::gAioInputDebug && (chosen != lastChosenDir || lastSource != lastLoggedSource)) {
            qDebug() << "[INPUT] chosen" << dirName(chosen) << "source" << sourceName(lastSource)
                     << "dpad" << dirName(dpadDir) << "stick" << dirName(stickDir)
                     << "ms" << (nowMs - lastSourceMs);
            lastChosenDir = chosen;
            lastLoggedSource = lastSource;
        }

        if (chosen == Dir::Up) controllerLogical &= ~logicalMaskFor(LogicalButton::Up);
        if (chosen == Dir::Down) controllerLogical &= ~logicalMaskFor(LogicalButton::Down);
        if (chosen == Dir::Left) controllerLogical &= ~logicalMaskFor(LogicalButton::Left);
        if (chosen == Dir::Right) controllerLogical &= ~logicalMaskFor(LogicalButton::Right);
    }

    // Merge keyboard (latched by key events) with controller (polled every frame).
    logicalButtonsDown_ = keyboardLogicalButtonsDown_ & controllerLogical;
}

} // namespace AIO::Input
