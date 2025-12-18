#include "input/InputManager.h"

#include "input/ActionBindings.h"
#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QtGlobal>

namespace AIO::Input {

InputManager& InputManager::instance() {
    static InputManager instance;
    return instance;
}

InputManager::InputManager() {
    // Only initialize the GameController subsystem here.
    // SDL audio is initialized/owned by MainWindow; calling SDL_Quit() from this
    // singleton would shut down audio and other SDL subsystems globally.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        qWarning() << "SDL could not initialize! SDL Error:" << SDL_GetError();
    }

    // Enable with: export AIO_INPUT_DEBUG=1
    detail::gAioInputDebug = (qEnvironmentVariableIntValue("AIO_INPUT_DEBUG") != 0);

    loadControllerMappingRegistry();

    // Default logical -> GBA bindings (can be changed per emulator later).
    setGBALogicalBinding(LogicalButton::Confirm, Button_A);
    setGBALogicalBinding(LogicalButton::Back, Button_B);
    setGBALogicalBinding(LogicalButton::Select, Button_Select);
    setGBALogicalBinding(LogicalButton::Start, Button_Start);
    setGBALogicalBinding(LogicalButton::L, Button_L);
    setGBALogicalBinding(LogicalButton::R, Button_R);
    setGBALogicalBinding(LogicalButton::Up, Button_Up);
    setGBALogicalBinding(LogicalButton::Down, Button_Down);
    setGBALogicalBinding(LogicalButton::Left, Button_Left);
    setGBALogicalBinding(LogicalButton::Right, Button_Right);

    // App-wide UI keyboard defaults (10-foot UI expectations).
    setUIKeyBinding(LogicalButton::Confirm, Qt::Key_Return);
    setUIKeyBinding(LogicalButton::Back, Qt::Key_Escape);
    setUIKeyBinding(LogicalButton::Up, Qt::Key_Up);
    setUIKeyBinding(LogicalButton::Down, Qt::Key_Down);
    setUIKeyBinding(LogicalButton::Left, Qt::Key_Left);
    setUIKeyBinding(LogicalButton::Right, Qt::Key_Right);
    setUIKeyBinding(LogicalButton::Home, Qt::Key_Home);

    loadConfig();
}

InputManager::~InputManager() {
    saveConfig();

    for (auto c : controllers) {
        SDL_GameControllerClose(c);
    }

    // Do not call SDL_Quit() here; other parts of the app (audio) may still be using SDL.
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

bool InputManager::processKeyEvent(QKeyEvent* event) {
    const int key = event->key();

    // First: app-wide UI logical bindings.
    if (uiKeyToLogical_.contains(key)) {
        const LogicalButton logical = uiKeyToLogical_.value(key);
        const uint32_t mask = 1u << static_cast<uint32_t>(logical);

        if (event->type() == QEvent::KeyPress) {
            logicalButtonsDown_ &= ~mask; // pressed
        } else if (event->type() == QEvent::KeyRelease) {
            if (!event->isAutoRepeat()) {
                logicalButtonsDown_ |= mask; // released
            }
        }
        return true;
    }

    // Emulator-facing keyboard mapping (GBA buttons -> KEYINPUT)
    if (!keyToButtonMap.contains(key)) {
        return false;
    }

    const GBAButton btn = keyToButtonMap[key];
    const int bit = static_cast<int>(btn);

    if (event->type() == QEvent::KeyPress) {
        // 0 = Pressed
        keyboardState &= ~(1 << bit);
    } else if (event->type() == QEvent::KeyRelease) {
        // 1 = Released
        if (!event->isAutoRepeat()) {
            keyboardState |= (1 << bit);
        }
    }

    return true;
}

void InputManager::setUIKeyBinding(LogicalButton logical, int qtKey) {
    if (uiLogicalToKey_.contains(logical)) {
        const int oldKey = uiLogicalToKey_.value(logical);
        uiKeyToLogical_.remove(oldKey);
    }
    if (uiKeyToLogical_.contains(qtKey)) {
        const LogicalButton oldLogical = uiKeyToLogical_.value(qtKey);
        uiLogicalToKey_.remove(oldLogical);
    }
    uiKeyToLogical_[qtKey] = logical;
    uiLogicalToKey_[logical] = qtKey;
}

int InputManager::uiKeyBinding(LogicalButton logical) const {
    return uiLogicalToKey_.value(logical, Qt::Key_unknown);
}

InputSnapshot InputManager::updateSnapshot() {
    const uint16_t keyinput = update();
    InputSnapshot snapshot;
    snapshot.keyinput = keyinput;
    snapshot.logical = logicalButtonsDown_;
    snapshot.system = systemButtonsDown_;
    return snapshot;
}

bool InputManager::pressed(AppId app, ActionId action) const {
    const LogicalButton logical = ActionBindings::resolve(app, action);
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);
    return (logicalButtonsDown_ & mask) == 0;
}

bool InputManager::edgePressed(AppId app, ActionId action) const {
    const LogicalButton logical = ActionBindings::resolve(app, action);
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);
    const bool nowDown = (logicalButtonsDown_ & mask) == 0;
    const bool prevDown = (lastLogicalButtonsDown_ & mask) == 0;
    return nowDown && !prevDown;
}

} // namespace AIO::Input

