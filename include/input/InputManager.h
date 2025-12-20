#pragma once

#include <functional>
#include <QObject>
#include <QMap>
#include <QKeyEvent>
#include <SDL2/SDL.h>

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
     *   It intentionally does NOT call SDL_Quit() globally (audio is owned elsewhere).
     */
    class InputManager : public QObject {
        Q_OBJECT
    public:
        /** @brief Singleton accessor. */
        static InputManager& instance();

        /**
         * @brief Process a Qt key event and update internal keyboard/logical state.
         * @return true if the event was recognized and consumed.
         */
        bool processKeyEvent(QKeyEvent* event);

        /**
         * @brief Poll SDL and return a full snapshot for this frame.
         * Prefer this for UI code to avoid multiple global reads.
         */
        InputSnapshot updateSnapshot();

        /**
         * @brief Last snapshot produced by updateSnapshot() (no polling).
         */
        const InputSnapshot& snapshot() const { return lastSnapshot_; }

        /**
         * @brief Current logical (emulator-agnostic) input state.
         * 1 = released, 0 = pressed, same convention as GBA KEYINPUT.
         */
        uint32_t logicalButtonsDown() const { return logicalButtonsDown_; }

        /** @brief True if a logical action is currently pressed this frame. */
        bool pressed(LogicalButton logical) const;

        /** @brief True only on the transition from released -> pressed. */
        bool edgePressed(LogicalButton logical) const;

        /** @brief Canonical Qt key for a logical action (used for synthetic key events). */
        int canonicalQtKey(LogicalButton logical) const;

        /** @brief Current default bindings (single source of truth). */
        const InputBindings& bindings() const { return bindings_; }

        /**
         * @brief Bitmask of non-emulation "system" buttons pressed this frame.
         * Used for global UI actions (e.g. Home).
         */
        uint32_t systemButtonsDown() const { return systemButtonsDown_; }

        using Handler = std::function<void()>;
        void onPressed(LogicalButton logical, Handler handler);
        void dispatchPressedEdges();

    private:
        InputManager();
        ~InputManager();

        void pollSdl();

        QMap<int, SDL_GameController*> controllers;

        InputBindings bindings_;

        QMap<LogicalButton, Handler> pressHandlers_;

        // Current logical state (1 = released, 0 = pressed).
        uint32_t logicalButtonsDown_ = 0xFFFFFFFFu;

        // Keyboard-derived logical state (1 = released, 0 = pressed).
        // Maintained by processKeyEvent(); merged with controller state in update().
        uint32_t keyboardLogicalButtonsDown_ = 0xFFFFFFFFu;

        // Previous logical state for edge detection.
        uint32_t lastLogicalButtonsDown_ = 0xFFFFFFFFu;

        uint32_t systemButtonsDown_ = 0;

        InputSnapshot lastSnapshot_{};
        
    };

} // namespace Input
} // namespace AIO
