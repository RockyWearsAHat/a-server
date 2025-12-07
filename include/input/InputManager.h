#pragma once

#include <QObject>
#include <QMap>
#include <QKeyEvent>
#include <QSettings>
#include <SDL.h>

namespace AIO::Input {

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

        QMap<int, GBAButton> keyToButtonMap;
        QMap<GBAButton, int> buttonToKeyMap;

        QMap<int, GBAButton> gamepadToButtonMap;
        QMap<GBAButton, int> buttonToGamepadMap;
        
        QMap<int, SDL_GameController*> controllers;
        
        uint16_t keyboardState = 0xFFFF; // 1 = Released
        uint16_t gamepadState = 0xFFFF;
    };

}
