#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
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

// Per-controller stick processing state.
struct StickCenter {
  int lx = 0;
  int ly = 0;
  int rx = 0;
  int ry = 0;

  // Safe seeding: require a short period of near-rest before considering the
  // current position the true center.
  bool seededL = false;
  bool seededR = false;
  int seedCountL = 0;
  int seedCountR = 0;
};

struct StickDirState {
  bool lUp = false, lDown = false, lLeft = false, lRight = false;
  bool rUp = false, rDown = false, rLeft = false, rRight = false;

  // Debounce counters (press/release) per direction.
  uint8_t lUpPress = 0, lUpRelease = 0;
  uint8_t lDownPress = 0, lDownRelease = 0;
  uint8_t lLeftPress = 0, lLeftRelease = 0;
  uint8_t lRightPress = 0, lRightRelease = 0;
  uint8_t rUpPress = 0, rUpRelease = 0;
  uint8_t rDownPress = 0, rDownRelease = 0;
  uint8_t rLeftPress = 0, rLeftRelease = 0;
  uint8_t rRightPress = 0, rRightRelease = 0;
};

static QHash<SDL_GameController *, StickCenter> gStickCenters;
static QHash<SDL_GameController *, StickDirState> gStickStates;

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

  if (up)
    return Dir::Up;
  if (down)
    return Dir::Down;
  if (left)
    return Dir::Left;
  if (right)
    return Dir::Right;
  return Dir::None;
}

const char *dirName(Dir d) {
  switch (d) {
  case Dir::Up:
    return "Up";
  case Dir::Down:
    return "Down";
  case Dir::Left:
    return "Left";
  case Dir::Right:
    return "Right";
  default:
    return "None";
  }
}

const char *sourceName(DirSource s) {
  switch (s) {
  case DirSource::Dpad:
    return "Dpad";
  case DirSource::Stick:
    return "Stick";
  default:
    return "None";
  }
}

} // namespace

void InputManager::pollSdl() {
  // Capture previous logical state for edge detection.
  lastLogicalButtonsDown_.store(
      logicalButtonsDown_.load(std::memory_order_relaxed),
      std::memory_order_relaxed);

  // SDL GameController init may still be running in the background. Until
  // it completes, keep the UI responsive by using keyboard-only state.
  if (!sdlInitReady_.load(std::memory_order_acquire)) {
    systemButtonsDown_.store(0, std::memory_order_relaxed);
    logicalButtonsDown_.store(
        keyboardLogicalButtonsDown_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    return;
  }

  // Drain SDL events; we read current state via SDL_GameControllerGet* below.
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    // Intentionally consume controller events.
    if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
      lastControllerButtonDown_.store(static_cast<int>(ev.cbutton.button),
                                      std::memory_order_relaxed);
    }
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

    // Clear any per-controller cached processing state.
    gStickCenters.clear();
    gStickStates.clear();

    for (int i = 0; i < numJoysticks; ++i) {
      if (!SDL_IsGameController(i))
        continue;

      SDL_GameController *pad = SDL_GameControllerOpen(i);
      if (!pad)
        continue;

      controllers.insert(i, pad);
      const QString name = QString::fromUtf8(SDL_GameControllerName(pad));
      qDebug() << "Opened Gamepad:" << name;
    }

    lastNumJoysticks = numJoysticks;
  }

  // System buttons (Guide/Home/PS) are tracked separately from emulation input.
  systemButtonsDown_.store(0, std::memory_order_relaxed);

  // Controller-derived logical state is recomputed every frame (no latching).
  uint32_t controllerLogical = 0xFFFFFFFFu;

  const InputContext ctx = activeContext_.load(std::memory_order_relaxed);
  const bool emulatorContext = (ctx == InputContext::Emulator);

  // Unified direction provider state.
  static DirSource lastSource = DirSource::None;
  static qint64 lastSourceMs = 0;
  static QElapsedTimer sourceTimer;
  if (!sourceTimer.isValid())
    sourceTimer.start();

  static Dir lastDpadDir = Dir::None;
  static Dir lastStickDir = Dir::None;
  static Dir lastChosenDir = Dir::None;
  static DirSource lastLoggedSource = DirSource::None;

  for (auto pad : controllers) {
    if (detail::gAioInputDebug) {
      const int du =
          SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
      const int dd =
          SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
      const int dl =
          SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
      const int dr =
          SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

      const int lx = static_cast<int>(
          SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
      const int ly = static_cast<int>(
          SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
      const int rx = static_cast<int>(
          SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
      const int ry = static_cast<int>(
          SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

      qDebug() << "[INPUT] dpad" << du << dd << dl << dr << "axes" << lx << ly
               << rx << ry;
    }

    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE)) {
      systemButtonsDown_.store(
          systemButtonsDown_.load(std::memory_order_relaxed) | 0x1u,
          std::memory_order_relaxed);
    }

    // Non-direction logical buttons from mapping.
    const auto &buttonMap = emulatorContext
                                ? bindings_.emulator.controllerButtons
                                : bindings_.ui.controllerButtons;

    for (auto it = buttonMap.begin(); it != buttonMap.end(); ++it) {
      const int sdlBtn = it.key();
      const LogicalButton logical = it.value();
      if (SDL_GameControllerGetButton(
              pad, static_cast<SDL_GameControllerButton>(sdlBtn))) {
        controllerLogical &= ~logicalMaskFor(logical);
      }
    }

    // Direction intent: D-pad
    const bool dpadUp =
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    const bool dpadDown =
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    const bool dpadLeft =
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    const bool dpadRight =
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    const Dir dpadDir = collapseToSingle(dpadUp, dpadDown, dpadLeft, dpadRight);

    // Direction intent: sticks
    const int pressDeadzone = bindings_.sticks.pressDeadzone;
    const int releaseDeadzone = bindings_.sticks.releaseDeadzone;

    const int rawLx = static_cast<int>(
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    const int rawLy = static_cast<int>(
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    const int rawRx = static_cast<int>(
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
    const int rawRy = static_cast<int>(
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

    // Stick drift compensation:
    // Many controllers sit slightly off-center, and some report large bias
    // immediately after connect. Since GBA hardware had digital inputs,
    // treating this bias as a held direction is incorrect.
    //
    // We maintain a per-controller "center" and subtract it from raw axes.
    // The center is seeded on first sight, then slowly tracks the stick
    // only when it is near rest (within release deadzone).
    StickCenter &c = gStickCenters[pad];

    // Safe center seeding: only seed once we observe the stick near-rest
    // for a short consecutive period. This prevents a single biased sample
    // during connect/wake from becoming the baseline.
    constexpr int kSeedRequired = 6; // ~6 polls (e.g. ~6ms at 1kHz)
    auto nearRest = [&](int x, int y, int dz) {
      return (std::abs(x) < dz) && (std::abs(y) < dz);
    };

    if (!c.seededL) {
      if (nearRest(rawLx, rawLy, releaseDeadzone)) {
        c.seedCountL++;
        if (c.seedCountL >= kSeedRequired) {
          c.lx = rawLx;
          c.ly = rawLy;
          c.seededL = true;
        }
      } else {
        c.seedCountL = 0;
      }
    }

    if (!c.seededR) {
      if (nearRest(rawRx, rawRy, releaseDeadzone)) {
        c.seedCountR++;
        if (c.seedCountR >= kSeedRequired) {
          c.rx = rawRx;
          c.ry = rawRy;
          c.seededR = true;
        }
      } else {
        c.seedCountR = 0;
      }
    }

    auto nudgeCenter = [&](int &center, int raw, int releaseDz) {
      // Only adapt when close to rest.
      if (std::abs(raw - center) > releaseDz)
        return;
      // Low-pass filter: ~1/32 step.
      center += (raw - center) / 32;
    };

    if (c.seededL) {
      nudgeCenter(c.lx, rawLx, releaseDeadzone);
      nudgeCenter(c.ly, rawLy, releaseDeadzone);
    }
    if (c.seededR) {
      nudgeCenter(c.rx, rawRx, releaseDeadzone);
      nudgeCenter(c.ry, rawRy, releaseDeadzone);
    }

    const int lx = c.seededL ? (rawLx - c.lx) : 0;
    const int ly = c.seededL ? (rawLy - c.ly) : 0;
    const int rx = c.seededR ? (rawRx - c.rx) : 0;
    const int ry = c.seededR ? (rawRy - c.ry) : 0;

    // Stick direction mapping:
    // - UI context: collapse to one direction (menu-friendly)
    // - Emulator context: allow true diagonals (8-way)
    struct StickDirs {
      bool up = false;
      bool down = false;
      bool left = false;
      bool right = false;
    };

    StickDirState &st = gStickStates[pad];

    // Debounced Schmitt trigger: requires a condition to hold for N
    // consecutive polls before flipping state. This suppresses single-sample
    // spikes that otherwise appear as random left/right taps.
    constexpr uint8_t kDebounceN = 3;
    auto debouncedAxisDir = [&](bool &cur, uint8_t &pressCount,
                                uint8_t &releaseCount, int v, int pressDz,
                                int releaseDz, int sign) {
      const bool wantPress = (sign < 0) ? (v <= -pressDz) : (v >= pressDz);
      const bool wantRelease = (std::abs(v) < releaseDz);

      if (!cur) {
        if (wantPress) {
          if (pressCount < 255)
            pressCount++;
          if (pressCount >= kDebounceN) {
            cur = true;
            releaseCount = 0;
          }
        } else {
          pressCount = 0;
        }
      } else {
        if (wantRelease) {
          if (releaseCount < 255)
            releaseCount++;
          if (releaseCount >= kDebounceN) {
            cur = false;
            pressCount = 0;
          }
        } else {
          releaseCount = 0;
        }
      }
    };

    auto stickToDirs =
        [&](int x, int y, bool enabled, bool &up, bool &down, bool &left,
            bool &right, bool &stUp, bool &stDown, bool &stLeft, bool &stRight,
            uint8_t &upPress, uint8_t &upRelease, uint8_t &downPress,
            uint8_t &downRelease, uint8_t &leftPress, uint8_t &leftRelease,
            uint8_t &rightPress, uint8_t &rightRelease) {
          if (!enabled) {
            stUp = stDown = stLeft = stRight = false;
            upPress = upRelease = downPress = downRelease = 0;
            leftPress = leftRelease = rightPress = rightRelease = 0;
            return;
          }

          debouncedAxisDir(stLeft, leftPress, leftRelease, x, pressDeadzone,
                           releaseDeadzone, -1);
          debouncedAxisDir(stRight, rightPress, rightRelease, x, pressDeadzone,
                           releaseDeadzone, +1);
          debouncedAxisDir(stUp, upPress, upRelease, y, pressDeadzone,
                           releaseDeadzone, -1);
          debouncedAxisDir(stDown, downPress, downRelease, y, pressDeadzone,
                           releaseDeadzone, +1);

          up |= stUp;
          down |= stDown;
          left |= stLeft;
          right |= stRight;
        };

    StickDirs stickDirs;
    stickToDirs(lx, ly, bindings_.sticks.enableLeftStick, stickDirs.up,
                stickDirs.down, stickDirs.left, stickDirs.right, st.lUp,
                st.lDown, st.lLeft, st.lRight, st.lUpPress, st.lUpRelease,
                st.lDownPress, st.lDownRelease, st.lLeftPress, st.lLeftRelease,
                st.lRightPress, st.lRightRelease);
    stickToDirs(rx, ry, bindings_.sticks.enableRightStick, stickDirs.up,
                stickDirs.down, stickDirs.left, stickDirs.right, st.rUp,
                st.rDown, st.rLeft, st.rRight, st.rUpPress, st.rUpRelease,
                st.rDownPress, st.rDownRelease, st.rLeftPress, st.rLeftRelease,
                st.rRightPress, st.rRightRelease);

    // Resolve opposites.
    if (stickDirs.up && stickDirs.down) {
      stickDirs.up = false;
      stickDirs.down = false;
    }
    if (stickDirs.left && stickDirs.right) {
      stickDirs.left = false;
      stickDirs.right = false;
    }

    // For UI, produce a single "chosen" direction for action mapping.
    const Dir stickDir = [&]() {
      if (emulatorContext) {
        // Unused in emulatorContext.
        return Dir::None;
      }
      return collapseToSingle(stickDirs.up, stickDirs.down, stickDirs.left,
                              stickDirs.right);
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
    if (!emulatorContext) {
      if (stickDir != Dir::None && dpadDir != Dir::None) {
        chosen = (lastSource == DirSource::Stick) ? stickDir : dpadDir;
      } else if (stickDir != Dir::None) {
        chosen = stickDir;
      } else {
        chosen = dpadDir;
      }
    }

    if (detail::gAioInputDebug &&
        (chosen != lastChosenDir || lastSource != lastLoggedSource)) {
      qDebug() << "[INPUT] chosen" << dirName(chosen) << "source"
               << sourceName(lastSource) << "dpad" << dirName(dpadDir)
               << "stick" << dirName(stickDir) << "ms"
               << (nowMs - lastSourceMs);
      lastChosenDir = chosen;
      lastLoggedSource = lastSource;
    }

    if (!emulatorContext) {
      if (chosen == Dir::Up)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Up);
      if (chosen == Dir::Down)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Down);
      if (chosen == Dir::Left)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Left);
      if (chosen == Dir::Right)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Right);
    } else {
      // Emulator: allow true diagonals.
      bool up = dpadUp || stickDirs.up;
      bool down = dpadDown || stickDirs.down;
      bool left = dpadLeft || stickDirs.left;
      bool right = dpadRight || stickDirs.right;

      if (up && down) {
        up = false;
        down = false;
      }
      if (left && right) {
        left = false;
        right = false;
      }

      if (up)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Up);
      if (down)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Down);
      if (left)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Left);
      if (right)
        controllerLogical &= ~logicalMaskFor(LogicalButton::Right);
    }
  }

  // Merge keyboard (latched by key events) with controller (polled every
  // frame).
  const uint32_t kb =
      keyboardLogicalButtonsDown_.load(std::memory_order_relaxed);
  logicalButtonsDown_.store(kb & controllerLogical, std::memory_order_relaxed);
}

} // namespace AIO::Input
