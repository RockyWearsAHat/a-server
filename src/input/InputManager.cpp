#include "input/InputManager.h"

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

    bindings_ = DefaultInputBindings();
}

InputManager::~InputManager() {
    for (auto c : controllers) {
        SDL_GameControllerClose(c);
    }

    // Do not call SDL_Quit() here; other parts of the app (audio) may still be using SDL.
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void InputManager::setActiveContext(InputContext ctx) {
    if (activeContext_ == ctx) return;
    activeContext_ = ctx;

    // Clear latched keyboard state so keys don't get "stuck" when switching
    // between UI and emulator contexts.
    keyboardLogicalButtonsDown_ = 0xFFFFFFFFu;
    logicalButtonsDown_ = 0xFFFFFFFFu;
    lastLogicalButtonsDown_ = 0xFFFFFFFFu;
    systemButtonsDown_ = 0;
    lastSnapshot_ = InputSnapshot{};
}

bool InputManager::processKeyEvent(QKeyEvent* event) {
    const int key = event->key();

    const auto& keymap = (activeContext_ == InputContext::Emulator)
        ? bindings_.emulator.keyboard
        : bindings_.ui.keyboard;

    if (!keymap.contains(key)) return false;
    const LogicalButton logical = keymap.value(key);
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);

    if (event->type() == QEvent::KeyPress) {
        keyboardLogicalButtonsDown_ &= ~mask; // pressed
    } else if (event->type() == QEvent::KeyRelease) {
        if (!event->isAutoRepeat()) {
            keyboardLogicalButtonsDown_ |= mask; // released
        }
    }

    return true;
}

InputSnapshot InputManager::updateSnapshot() {
    pollSdl();
    InputSnapshot snapshot;

    // Provide a legacy GBA KEYINPUT view (active-low), derived from logical.
    // Bit layout: 0=A,1=B,2=Select,3=Start,4=Right,5=Left,6=Up,7=Down,8=R,9=L
    uint16_t keyinput = 0x03FF;
    auto apply = [&](LogicalButton logical, int bit) {
        const uint32_t mask = 1u << static_cast<uint32_t>(logical);
        if ((logicalButtonsDown_ & mask) == 0) {
            keyinput = static_cast<uint16_t>(keyinput & ~(1u << bit));
        }
    };

    // Default mapping: UI-style Confirm/Back become GBA A/B.
    apply(LogicalButton::Confirm, 0);
    apply(LogicalButton::Back, 1);
    apply(LogicalButton::Select, 2);
    apply(LogicalButton::Start, 3);
    apply(LogicalButton::Right, 4);
    apply(LogicalButton::Left, 5);
    apply(LogicalButton::Up, 6);
    apply(LogicalButton::Down, 7);
    apply(LogicalButton::R, 8);
    apply(LogicalButton::L, 9);

    snapshot.keyinput = keyinput;
    snapshot.logical = logicalButtonsDown_;
    snapshot.system = systemButtonsDown_;
    lastSnapshot_ = snapshot;
    return snapshot;
}

bool InputManager::pressed(LogicalButton logical) const {
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);
    return (logicalButtonsDown_ & mask) == 0;
}

bool InputManager::edgePressed(LogicalButton logical) const {
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);
    const bool nowDown = (logicalButtonsDown_ & mask) == 0;
    const bool prevDown = (lastLogicalButtonsDown_ & mask) == 0;
    return nowDown && !prevDown;
}

int InputManager::canonicalQtKey(LogicalButton logical) const {
    return bindings_.canonicalQtKeys.value(logical, Qt::Key_unknown);
}

void InputManager::onPressed(LogicalButton logical, Handler handler) {
    pressHandlers_[logical] = std::move(handler);
}

void InputManager::dispatchPressedEdges() {
    for (auto it = pressHandlers_.begin(); it != pressHandlers_.end(); ++it) {
        const LogicalButton logical = it.key();
        if (!edgePressed(logical)) continue;
        if (it.value()) it.value()();
    }
}

} // namespace AIO::Input

