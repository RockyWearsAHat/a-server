#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QSettings>
#include <QtGlobal>

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace AIO::Input {

namespace {
QSettings &InputSettings() {
  static QSettings s("AIOServer", "AIOInput");
  return s;
}

static QString CtxName(InputContext ctx) {
  return (ctx == InputContext::Emulator) ? QStringLiteral("Emulator")
                                         : QStringLiteral("UI");
}

template <typename T>
static void RemoveAnyMappingToLogical(QMap<int, T> &map, T logical) {
  for (auto it = map.begin(); it != map.end();) {
    if (it.value() == logical)
      it = map.erase(it);
    else
      ++it;
  }
}

} // namespace

InputManager &InputManager::instance() {
  // Intentionally leaked singleton.
  //
  // Rationale (macOS): SDL's controller/HID init and teardown can interact
  // poorly with Qt/CFRunLoop shutdown ordering, causing crashes (SIGTRAP/
  // EXC_BAD_ACCESS) or long stalls when joining the init thread at process
  // exit. Leaking the singleton avoids global destructor teardown entirely;
  // the OS will reclaim resources on process exit.
  static InputManager *instance = new InputManager();
  return *instance;
}

InputManager::InputManager() {
  // Enable with: export AIO_INPUT_DEBUG=1
  detail::gAioInputDebug =
      (qEnvironmentVariableIntValue("AIO_INPUT_DEBUG") != 0);

  bindings_ = DefaultInputBindings();

  // Apply user overrides (if any) on top of defaults.
  {
    auto &s = InputSettings();

    for (InputContext ctx : {InputContext::UI, InputContext::Emulator}) {
      // Keyboard overrides: qtKey -> logical
      s.beginGroup(QStringLiteral("input/keyboard/%1").arg(CtxName(ctx)));
      const auto keys = s.allKeys();
      auto &km = (ctx == InputContext::Emulator) ? bindings_.emulator.keyboard
                                                 : bindings_.ui.keyboard;
      for (const auto &k : keys) {
        bool ok = false;
        const int qtKey = k.toInt(&ok);
        if (!ok)
          continue;
        const int logicalInt = s.value(k).toInt();
        km[qtKey] = static_cast<LogicalButton>(logicalInt);
      }
      s.endGroup();

      // Controller overrides: sdlBtn -> logical
      s.beginGroup(QStringLiteral("input/controller/%1").arg(CtxName(ctx)));
      const auto btnKeys = s.allKeys();
      auto &cm = (ctx == InputContext::Emulator)
                     ? bindings_.emulator.controllerButtons
                     : bindings_.ui.controllerButtons;
      for (const auto &k : btnKeys) {
        bool ok = false;
        const int btn = k.toInt(&ok);
        if (!ok)
          continue;
        const int logicalInt = s.value(k).toInt();
        cm[btn] = static_cast<LogicalButton>(logicalInt);
      }
      s.endGroup();
    }

    // Stick config overrides
    s.beginGroup(QStringLiteral("input/sticks"));
    bindings_.sticks.pressDeadzone =
        s.value(QStringLiteral("pressDeadzone"), bindings_.sticks.pressDeadzone)
            .toInt();
    bindings_.sticks.releaseDeadzone =
        s.value(QStringLiteral("releaseDeadzone"),
                bindings_.sticks.releaseDeadzone)
            .toInt();
    bindings_.sticks.enableLeftStick =
        s.value(QStringLiteral("enableLeftStick"),
                bindings_.sticks.enableLeftStick)
            .toBool();
    bindings_.sticks.enableRightStick =
        s.value(QStringLiteral("enableRightStick"),
                bindings_.sticks.enableRightStick)
            .toBool();
    s.endGroup();
  }

  // SDL controller initialization can block for a long time on macOS.
  // Start it on a background thread so the launcher UI stays responsive.
  startSdlControllerInitAsync();

  // Global high-frequency polling (sub-ms). Consumers read snapshot() and do
  // not need to call updateSnapshot() on their own.
  startPollingThread();
}

void InputManager::setStickDeadzones(int pressDeadzone, int releaseDeadzone) {
  if (pressDeadzone < 0)
    pressDeadzone = 0;
  if (releaseDeadzone < 0)
    releaseDeadzone = 0;
  if (releaseDeadzone > pressDeadzone) {
    releaseDeadzone = pressDeadzone;
  }
  bindings_.sticks.pressDeadzone = pressDeadzone;
  bindings_.sticks.releaseDeadzone = releaseDeadzone;

  auto &s = InputSettings();
  s.beginGroup(QStringLiteral("input/sticks"));
  s.setValue(QStringLiteral("pressDeadzone"), pressDeadzone);
  s.setValue(QStringLiteral("releaseDeadzone"), releaseDeadzone);
  s.endGroup();
}

int InputManager::consumeLastControllerButtonDown() {
  return lastControllerButtonDown_.exchange(-1, std::memory_order_acq_rel);
}

void InputManager::rebindKeyboard(InputContext ctx, LogicalButton logical,
                                  int qtKey) {
  auto &km = (ctx == InputContext::Emulator) ? bindings_.emulator.keyboard
                                             : bindings_.ui.keyboard;
  km.remove(qtKey);
  RemoveAnyMappingToLogical(km, logical);
  km[qtKey] = logical;

  auto &s = InputSettings();
  s.beginGroup(QStringLiteral("input/keyboard/%1").arg(CtxName(ctx)));
  s.remove(QString());
  for (auto it = km.begin(); it != km.end(); ++it) {
    s.setValue(QString::number(it.key()), static_cast<int>(it.value()));
  }
  s.endGroup();
}

void InputManager::rebindControllerButton(InputContext ctx,
                                          LogicalButton logical,
                                          int sdlButton) {
  auto &cm = (ctx == InputContext::Emulator)
                 ? bindings_.emulator.controllerButtons
                 : bindings_.ui.controllerButtons;
  cm.remove(sdlButton);
  RemoveAnyMappingToLogical(cm, logical);
  cm[sdlButton] = logical;

  auto &s = InputSettings();
  s.beginGroup(QStringLiteral("input/controller/%1").arg(CtxName(ctx)));
  s.remove(QString());
  for (auto it = cm.begin(); it != cm.end(); ++it) {
    s.setValue(QString::number(it.key()), static_cast<int>(it.value()));
  }
  s.endGroup();
}

int InputManager::primaryKeyboardKeyFor(InputContext ctx,
                                        LogicalButton logical) const {
  const auto &km = (ctx == InputContext::Emulator) ? bindings_.emulator.keyboard
                                                   : bindings_.ui.keyboard;
  for (auto it = km.begin(); it != km.end(); ++it) {
    if (it.value() == logical)
      return it.key();
  }
  return -1;
}

int InputManager::primaryControllerButtonFor(InputContext ctx,
                                             LogicalButton logical) const {
  const auto &cm = (ctx == InputContext::Emulator)
                       ? bindings_.emulator.controllerButtons
                       : bindings_.ui.controllerButtons;
  for (auto it = cm.begin(); it != cm.end(); ++it) {
    if (it.value() == logical)
      return it.key();
  }
  return -1;
}

InputManager::~InputManager() {
  // Intentionally avoid SDL teardown here.
  //
  // On macOS, SDL's HID shutdown (IOHIDManagerUnscheduleFromRunLoop) can
  // crash if it runs during global destructor teardown after Qt has already
  // torn down parts of the CFRunLoop machinery.
  //
  // The app must call Shutdown() (wired from main via aboutToQuit) while Qt
  // is still alive.
  if (sdlInitThread_.joinable()) {
    sdlInitThread_.join();
  }
}

void InputManager::Shutdown() {
  bool expected = false;
  if (!sdlShutdown_.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
    return;
  }

  if (sdlInitThread_.joinable()) {
    sdlInitThread_.join();
  }

  stopPollingThread();
}

void InputManager::setActiveContext(InputContext ctx) {
  const InputContext prev = activeContext_.load(std::memory_order_relaxed);
  if (prev == ctx)
    return;
  activeContext_.store(ctx, std::memory_order_relaxed);

  // Clear latched keyboard state so keys don't get "stuck" when switching
  // between UI and emulator contexts.
  keyboardLogicalButtonsDown_.store(0xFFFFFFFFu, std::memory_order_relaxed);
  logicalButtonsDown_.store(0xFFFFFFFFu, std::memory_order_relaxed);
  lastLogicalButtonsDown_.store(0xFFFFFFFFu, std::memory_order_relaxed);
  systemButtonsDown_.store(0, std::memory_order_relaxed);
  lastSnapshot_.store(InputSnapshot{}, std::memory_order_relaxed);
}

bool InputManager::processKeyEvent(QKeyEvent *event) {
  const int key = event->key();

  const auto &keymap =
      (activeContext_.load(std::memory_order_relaxed) == InputContext::Emulator)
          ? bindings_.emulator.keyboard
          : bindings_.ui.keyboard;

  if (!keymap.contains(key))
    return false;
  const LogicalButton logical = keymap.value(key);
  const uint32_t mask = 1u << static_cast<uint32_t>(logical);

  if (event->type() == QEvent::KeyPress) {
    // pressed
    uint32_t cur = keyboardLogicalButtonsDown_.load(std::memory_order_relaxed);
    while (!keyboardLogicalButtonsDown_.compare_exchange_weak(
        cur, (cur & ~mask), std::memory_order_relaxed)) {
    }
  } else if (event->type() == QEvent::KeyRelease) {
    if (!event->isAutoRepeat()) {
      // released
      uint32_t cur =
          keyboardLogicalButtonsDown_.load(std::memory_order_relaxed);
      while (!keyboardLogicalButtonsDown_.compare_exchange_weak(
          cur, (cur | mask), std::memory_order_relaxed)) {
      }
    }
  }

  return true;
}

InputSnapshot InputManager::updateSnapshot() {
  std::lock_guard<std::mutex> lock(pollMutex_);
  return pollAndPublishSnapshotLocked();
}

InputSnapshot InputManager::pollNow() { return updateSnapshot(); }

InputSnapshot InputManager::snapshot() const {
  return lastSnapshot_.load(std::memory_order_relaxed);
}

InputSnapshot InputManager::pollAndPublishSnapshotLocked() {
  pollSdl();
  InputSnapshot snapshot;

  // Provide a legacy GBA KEYINPUT view (active-low), derived from logical.
  // Mapping lives in InputBindings (defaults + user overrides).
  uint16_t keyinput = 0x03FF;
  const uint32_t logicalNow =
      logicalButtonsDown_.load(std::memory_order_relaxed);
  for (auto it = bindings_.gba.keyinputBits.begin();
       it != bindings_.gba.keyinputBits.end(); ++it) {
    const LogicalButton logical = it.key();
    const int bit = it.value();
    if (bit < 0 || bit > 15)
      continue;
    const uint32_t mask = 1u << static_cast<uint32_t>(logical);
    // Project convention: logical buttons are active-low (0 = pressed).
    // GBA KEYINPUT is also active-low (0 = pressed).
    if ((logicalNow & mask) == 0) {
      keyinput = static_cast<uint16_t>(keyinput & ~(1u << bit));
    }
  }

  snapshot.keyinput = keyinput;
  snapshot.logical = logicalButtonsDown_.load(std::memory_order_relaxed);
  snapshot.system = systemButtonsDown_.load(std::memory_order_relaxed);
  lastSnapshot_.store(snapshot, std::memory_order_relaxed);
  return snapshot;
}

void InputManager::startSdlControllerInitAsync() {
  bool expected = false;
  if (!sdlInitStarted_.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
    return;
  }

  sdlInitThread_ = std::thread([this]() {
    const int rc = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    if (rc < 0) {
      sdlInitFailed_.store(true, std::memory_order_release);
      sdlInitReady_.store(false, std::memory_order_release);
      std::cerr << "[InputManager] SDL controller init failed: "
                << SDL_GetError() << std::endl;
      return;
    }

    sdlInitReady_.store(true, std::memory_order_release);
  });
}

bool InputManager::pressed(LogicalButton logical) const {
  const uint32_t mask = 1u << static_cast<uint32_t>(logical);
  return (logicalButtonsDown_.load(std::memory_order_relaxed) & mask) == 0;
}

bool InputManager::edgePressed(LogicalButton logical) const {
  const uint32_t mask = 1u << static_cast<uint32_t>(logical);
  const bool nowDown =
      (logicalButtonsDown_.load(std::memory_order_relaxed) & mask) == 0;
  const bool prevDown =
      (lastLogicalButtonsDown_.load(std::memory_order_relaxed) & mask) == 0;
  return nowDown && !prevDown;
}

void InputManager::startPollingThread() {
  // SDL controller/event pumping is not reliably thread-safe across platforms,
  // and on macOS it can break controller input if done off the main thread.
  // Default: disabled. Opt-in for experimentation via AIO_INPUT_POLL_THREAD=1.
  if (qEnvironmentVariableIntValue("AIO_INPUT_POLL_THREAD") != 1) {
    return;
  }

  pollThreadStop_.store(false, std::memory_order_release);

  int pollUs = 500; // 0.5ms default
  if (const char *v = std::getenv("AIO_INPUT_POLL_US")) {
    const int parsed = std::atoi(v);
    if (parsed >= 100 && parsed <= 5000) {
      pollUs = parsed;
    }
  }

  pollThread_ = std::thread([this, pollUs]() {
    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::microseconds(pollUs);
    auto next = Clock::now();
    while (!pollThreadStop_.load(std::memory_order_acquire) &&
           !sdlShutdown_.load(std::memory_order_acquire)) {
      next += interval;
      {
        std::lock_guard<std::mutex> lock(pollMutex_);
        pollAndPublishSnapshotLocked();
      }

      const auto now = Clock::now();
      if (now < next) {
        std::this_thread::sleep_until(next);
      } else {
        // Drop accumulated lag.
        next = now;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    }
  });
}

void InputManager::stopPollingThread() {
  pollThreadStop_.store(true, std::memory_order_release);
  if (pollThread_.joinable()) {
    pollThread_.join();
  }
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

} // namespace AIO::Input
