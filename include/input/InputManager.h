#pragma once

#include <QObject>
#include <QMap>
#include <QKeyEvent>
#include <QJsonObject>
#include <QSettings>
#include <SDL2/SDL.h>

#include "input/InputTypes.h"
#include "input/AppActions.h"

namespace AIO {
namespace Input {

enum GBAButton {
        Button_A = 0,
        Button_B = 1,
        Button_Select = 2,
        Button_Start = 3,
        Button_Right = 4,
        Button_Left = 5,
        Button_Up = 6,
        Button_Down = 7,
        Button_R = 8,
        Button_L = 9,
        Button_Count = 10
    };

    class InputManager : public QObject {
        Q_OBJECT
    public:
        static InputManager& instance();

        // Returns true if the event was handled
        bool processKeyEvent(QKeyEvent* event);
        
        // Polls SDL events and returns combined input state
        uint16_t update();

        // Polls SDL and returns a full snapshot for this frame.
        // Prefer this for UI code to avoid multiple global reads.
        InputSnapshot updateSnapshot();

        // Logical (emulator-agnostic) input state.
        // 1 = released, 0 = pressed, same convention as GBA KEYINPUT.
        uint32_t logicalButtonsDown() const { return logicalButtonsDown_; }

        // Action-based queries (AppId/ActionId -> resolved LogicalButton).
        bool pressed(AppId app, ActionId action) const;
        bool edgePressed(AppId app, ActionId action) const;

        // Configure how logical buttons map to GBA buttons (for the GBA core).
        // This lets us keep controller mapping global, and swap the emulator mapping separately.
        void setGBALogicalBinding(LogicalButton logical, GBAButton gbaButton);

        // App-wide UI keyboard bindings (independent from emulator keybinds).
        void setUIKeyBinding(LogicalButton logical, int qtKey);
        int uiKeyBinding(LogicalButton logical) const;

        // Returns a bitmask of non-emulation "system" buttons pressed this frame.
        // These are used for global UI actions (e.g., Home).
        uint32_t systemButtonsDown() const { return systemButtonsDown_; }

        ControllerFamily activeControllerFamily() const { return activeFamily_; }
        QString activeControllerName() const { return activeControllerName_; }

        // Map a Qt Key to a GBA Button
        void setMapping(int qtKey, GBAButton button);
        int getKeyForButton(GBAButton button) const;
        
        // Map a SDL Gamepad Button to a GBA Button
        void setGamepadMapping(int sdlButton, GBAButton button);
        int getGamepadButtonForButton(GBAButton button) const;

        QString getButtonName(GBAButton button) const;
        QString getGamepadButtonName(int sdlButton) const;
        
        void loadConfig();
        void saveConfig();

    private:
        InputManager();
        ~InputManager();

        void loadControllerMappingRegistry();
        void applyBestControllerLayoutForActivePad();
        void setLogicalMapping(LogicalButton logical, int sdlButton);

        QMap<int, GBAButton> keyToButtonMap;
        QMap<GBAButton, int> buttonToKeyMap;

        QMap<int, GBAButton> gamepadToButtonMap;
        QMap<GBAButton, int> buttonToGamepadMap;
        
        QMap<int, SDL_GameController*> controllers;

        // Logical mapping: SDL button -> LogicalButton
        QMap<int, LogicalButton> sdlToLogical_;
        // Emulator mapping: LogicalButton -> GBAButton
        QMap<LogicalButton, GBAButton> logicalToGBA_;

        // UI mapping: Qt::Key -> LogicalButton
        QMap<int, LogicalButton> uiKeyToLogical_;
        QMap<LogicalButton, int> uiLogicalToKey_;

        // Current logical state (1 = released, 0 = pressed).
        uint32_t logicalButtonsDown_ = 0xFFFFFFFFu;

        // Previous logical state for edge detection.
        uint32_t lastLogicalButtonsDown_ = 0xFFFFFFFFu;

        ControllerFamily activeFamily_ = ControllerFamily::Unknown;
        QString activeControllerName_;

        // Loaded from assets/controller_mappings.json
        QJsonObject controllerRegistryDoc_;

        uint32_t systemButtonsDown_ = 0;
        
        uint16_t keyboardState = 0xFFFF; // 1 = Released
        uint16_t gamepadState = 0xFFFF;
    };

} // namespace Input
} // namespace AIO
