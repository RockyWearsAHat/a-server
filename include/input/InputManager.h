#pragma once

#include <QKeyEvent>
#include <QMap>
#include <QObject>
#include <QString>
#include <SDL2/SDL.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "input/InputBindings.h"
#include "input/InputTypes.h"

namespace AIO {
namespace Input {

/**
 * @brief Global input manager for keyboard + SDL game controllers.
 *
 * Design goals:
 * - Poll all supported devices.
 * - Map physical inputs into a single logical action space (LogicalButton).
 * - Expose one contiguous state snapshot for all consumers.
 *
 * Semantics:
 * - Logical state uses the GBA convention: 1 = released, 0 = pressed.
 * - edgePressed() is computed from the previous frame snapshot.
 *
 * Ownership:
 * - This singleton initializes only SDL's GameController + Events subsystems.
 *   It intentionally does NOT call SDL_Quit() globally (audio is owned
 * elsewhere).
 */
class InputManager : public QObject {
  Q_OBJECT
public:
  /** @brief Singleton accessor. */
  static InputManager &instance();

  /**
   * @brief Explicitly shut down SDL input subsystems.
   *
   * This must be called before Qt's teardown on macOS to avoid CoreFoundation
   * run loop issues during global destructor shutdown.
   */
  void Shutdown();

  /**
   * @brief Select which binding context is active.
   * UI and Emulator can use different default mappings.
   */
  void setActiveContext(InputContext ctx);
  InputContext activeContext() const {
    return activeContext_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Process a Qt key event and update internal keyboard/logical state.
   * @return true if the event was recognized and consumed.
   */
  bool processKeyEvent(QKeyEvent *event);

  /**
   * @brief Poll SDL and return a full snapshot for this frame.
   * Prefer this for UI code to avoid multiple global reads.
   */
  InputSnapshot updateSnapshot();

  /**
   * @brief Force one synchronous poll and publish a fresh snapshot.
   * Useful for transition points (e.g., starting emulation).
   */
  InputSnapshot pollNow();

  /**
   * @brief Most recent snapshot produced by the polling loop.
   * Returned by value to be thread-safe.
   */
  InputSnapshot snapshot() const;

  /**
   * @brief Current logical (emulator-agnostic) input state.
   * 1 = released, 0 = pressed, same convention as GBA KEYINPUT.
   */
  uint32_t logicalButtonsDown() const {
    return logicalButtonsDown_.load(std::memory_order_relaxed);
  }

  /** @brief True if a logical action is currently pressed this frame. */
  bool pressed(LogicalButton logical) const;

  /** @brief True only on the transition from released -> pressed. */
  bool edgePressed(LogicalButton logical) const;

  /** @brief Canonical Qt key for a logical action (used for synthetic key
   * events). */
  int canonicalQtKey(LogicalButton logical) const;

  /** @brief Current default bindings (single source of truth). */
  const InputBindings &bindings() const { return bindings_; }

  // --- Customization / rebinding (used by emulator settings menus) ---
  // Simple semantics: one physical key/button per logical action.
  void rebindKeyboard(InputContext ctx, LogicalButton logical, int qtKey);
  void rebindControllerButton(InputContext ctx, LogicalButton logical,
                              int sdlButton);

  // Query helpers for UI.
  int primaryKeyboardKeyFor(InputContext ctx, LogicalButton logical) const;
  int primaryControllerButtonFor(InputContext ctx, LogicalButton logical) const;

  // Consume the most recent SDL controller button-down event observed by
  // pollSdl(). Returns SDL_CONTROLLER_BUTTON_* (as int) or -1 if none.
  int consumeLastControllerButtonDown();

  // Stick tuning (deadzone).
  int stickPressDeadzone() const { return bindings_.sticks.pressDeadzone; }
  int stickReleaseDeadzone() const { return bindings_.sticks.releaseDeadzone; }
  void setStickDeadzones(int pressDeadzone, int releaseDeadzone);

  /**
   * @brief Bitmask of non-emulation "system" buttons pressed this frame.
   * Used for global UI actions (e.g. Home).
   */
  uint32_t systemButtonsDown() const {
    return systemButtonsDown_.load(std::memory_order_relaxed);
  }

  using Handler = std::function<void()>;
  void onPressed(LogicalButton logical, Handler handler);
  void dispatchPressedEdges();

private:
  InputManager();
  ~InputManager();

  void startSdlControllerInitAsync();

  void startPollingThread();
  void stopPollingThread();
  InputSnapshot pollAndPublishSnapshotLocked();

  void pollSdl();

  QMap<int, SDL_GameController *> controllers;

  InputBindings bindings_;

  std::atomic<InputContext> activeContext_{InputContext::UI};

  QMap<LogicalButton, Handler> pressHandlers_;

  // Current logical state (1 = released, 0 = pressed).
  std::atomic<uint32_t> logicalButtonsDown_{0xFFFFFFFFu};

  // Keyboard-derived logical state (1 = released, 0 = pressed).
  // Maintained by processKeyEvent(); merged with controller state in update().
  std::atomic<uint32_t> keyboardLogicalButtonsDown_{0xFFFFFFFFu};

  // Previous logical state for edge detection.
  std::atomic<uint32_t> lastLogicalButtonsDown_{0xFFFFFFFFu};

  std::atomic<uint32_t> systemButtonsDown_{0};

  std::atomic<InputSnapshot> lastSnapshot_{InputSnapshot{}};

  // Prevent concurrent SDL polling from multiple threads.
  mutable std::mutex pollMutex_;

  std::atomic<bool> pollThreadStop_{false};
  std::thread pollThread_;

  // Most recent SDL controller button-down (for rebinding UI).
  std::atomic<int> lastControllerButtonDown_{-1};

  // SDL GameController init can block for a long time on macOS (HID/device
  // enumeration). To keep the launcher UI responsive, we initialize it on
  // a background thread and simply ignore controller input until ready.
  std::atomic<bool> sdlInitStarted_{false};
  std::atomic<bool> sdlInitReady_{false};
  std::atomic<bool> sdlInitFailed_{false};
  std::thread sdlInitThread_;

  std::atomic<bool> sdlShutdown_{false};
};

} // namespace Input
} // namespace AIO
