#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QtGlobal>
#include <algorithm>

namespace AIO::Input {

InputManager &InputManager::instance() {
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
  detail::gAioInputDebug =
      (qEnvironmentVariableIntValue("AIO_INPUT_DEBUG") != 0);

  bindings_ = DefaultInputBindings();

  // In the current configuration we perform controller init synchronously on
  // the main thread. Mark SDL input as ready so pollSdl() can begin merging
  // controller state immediately.
  sdlInitStarted_.store(true, std::memory_order_relaxed);
  sdlInitReady_.store(true, std::memory_order_release);
}

InputManager::~InputManager() { Shutdown(); }

void InputManager::Shutdown() {
  bool expected = false;
  if (!sdlShutdown_.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
    // Already shut down.
    return;
  }

  // Stop any background threads if they are ever started in the future.
  pollThreadStop_.store(true, std::memory_order_relaxed);
  if (pollThread_.joinable()) {
    pollThread_.join();
  }
  if (sdlInitThread_.joinable()) {
    sdlInitThread_.join();
  }

  for (auto c : controllers) {
    SDL_GameControllerClose(c);
  }
  controllers.clear();

  // Safe to call multiple times; SDL ignores redundant quits.
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

void InputManager::setActiveContext(InputContext ctx) {
  if (activeContext_ == ctx)
    return;
  activeContext_ = ctx;

  // Clear latched keyboard state so keys don't get "stuck" when switching
  // between UI and emulator contexts.
  keyboardLogicalButtonsDown_ = 0xFFFFFFFFu;
  logicalButtonsDown_ = 0xFFFFFFFFu;
  lastLogicalButtonsDown_ = 0xFFFFFFFFu;
  systemButtonsDown_ = 0;
  lastSnapshot_ = InputSnapshot{};
}

bool InputManager::processKeyEvent(QKeyEvent *event) {
  const int key = event->key();

  const auto &keymap = (activeContext_ == InputContext::Emulator)
                           ? bindings_.emulator.keyboard
                           : bindings_.ui.keyboard;

  if (!keymap.contains(key))
    return false;
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
  std::lock_guard<std::mutex> lock(pollMutex_);
  return pollAndPublishSnapshotLocked();
}

InputSnapshot InputManager::pollNow() {
  std::lock_guard<std::mutex> lock(pollMutex_);
  return pollAndPublishSnapshotLocked();
}

InputSnapshot InputManager::snapshot() const {
  return lastSnapshot_.load(std::memory_order_acquire);
}

InputSnapshot InputManager::pollAndPublishSnapshotLocked() {
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
  lastSnapshot_.store(snapshot, std::memory_order_release);
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
    if (!edgePressed(logical))
      continue;
    if (it.value())
      it.value()();
  }
}

void InputManager::rebindKeyboard(InputContext ctx, LogicalButton logical,
                                  int qtKey) {
  InputBindings::ContextBindings *target =
      (ctx == InputContext::Emulator) ? &bindings_.emulator : &bindings_.ui;

  // Ensure a single physical key per logical action.
  for (auto it = target->keyboard.begin(); it != target->keyboard.end();) {
    if (it.value() == logical) {
      it = target->keyboard.erase(it);
    } else {
      ++it;
    }
  }

  if (qtKey != 0) {
    (*target).keyboard[qtKey] = logical;
  }
}

void InputManager::rebindControllerButton(InputContext ctx,
                                          LogicalButton logical,
                                          int sdlButton) {
  InputBindings::ContextBindings *target =
      (ctx == InputContext::Emulator) ? &bindings_.emulator : &bindings_.ui;

  // Remove any existing binding for this logical action.
  for (auto it = target->controllerButtons.begin();
       it != target->controllerButtons.end();) {
    if (it.value() == logical) {
      it = target->controllerButtons.erase(it);
    } else {
      ++it;
    }
  }

  if (sdlButton >= 0) {
    (*target).controllerButtons[sdlButton] = logical;
  }
}

int InputManager::primaryKeyboardKeyFor(InputContext ctx,
                                        LogicalButton logical) const {
  const InputBindings::ContextBindings &target =
      (ctx == InputContext::Emulator) ? bindings_.emulator : bindings_.ui;
  for (auto it = target.keyboard.constBegin(); it != target.keyboard.constEnd();
       ++it) {
    if (it.value() == logical) {
      return it.key();
    }
  }
  return 0;
}

int InputManager::primaryControllerButtonFor(InputContext ctx,
                                             LogicalButton logical) const {
  const InputBindings::ContextBindings &target =
      (ctx == InputContext::Emulator) ? bindings_.emulator : bindings_.ui;
  for (auto it = target.controllerButtons.constBegin();
       it != target.controllerButtons.constEnd(); ++it) {
    if (it.value() == logical) {
      return it.key();
    }
  }
  return -1;
}

int InputManager::consumeLastControllerButtonDown() {
  return lastControllerButtonDown_.exchange(-1, std::memory_order_acq_rel);
}

void InputManager::setStickDeadzones(int pressDeadzone, int releaseDeadzone) {
  pressDeadzone = std::max(0, pressDeadzone);
  releaseDeadzone = std::max(0, releaseDeadzone);
  if (pressDeadzone < releaseDeadzone) {
    pressDeadzone = releaseDeadzone;
  }
  bindings_.sticks.pressDeadzone = pressDeadzone;
  bindings_.sticks.releaseDeadzone = releaseDeadzone;
}

} // namespace AIO::Input
