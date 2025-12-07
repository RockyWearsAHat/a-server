#include "input/InputManager.h"
#include <QDebug>
#include <iostream>

namespace AIO::Input {

    InputManager& InputManager::instance() {
        static InputManager _instance;
        return _instance;
    }

    InputManager::InputManager() {
        if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
            qWarning() << "SDL could not initialize! SDL Error:" << SDL_GetError();
        }
        loadConfig();
    }

    InputManager::~InputManager() {
        saveConfig();
        for (auto c : controllers) {
            SDL_GameControllerClose(c);
        }
        SDL_Quit();
    }

    bool InputManager::processKeyEvent(QKeyEvent* event) {
        int key = event->key();
        
        std::cout << "[INPUT] Key event: key=" << key << " type=" 
                  << (event->type() == QEvent::KeyPress ? "Press" : "Release")
                  << " mapped=" << keyToButtonMap.contains(key) << std::endl;
        
        if (!keyToButtonMap.contains(key)) {
            return false;
        }

        GBAButton btn = keyToButtonMap[key];
        int bit = static_cast<int>(btn);

        if (event->type() == QEvent::KeyPress) {
            // 0 = Pressed
            keyboardState &= ~(1 << bit);
            std::cout << "[INPUT] Button " << getButtonName(btn).toStdString() 
                      << " PRESSED, keyboardState=0x" << std::hex << keyboardState << std::dec << std::endl;
        } else if (event->type() == QEvent::KeyRelease) {
            // 1 = Released
            if (!event->isAutoRepeat()) {
                keyboardState |= (1 << bit);
                std::cout << "[INPUT] Button " << getButtonName(btn).toStdString() 
                          << " RELEASED, keyboardState=0x" << std::hex << keyboardState << std::dec << std::endl;
            }
        }

        return true;
    }

    uint16_t InputManager::update() {
        SDL_PumpEvents();

        // Check for new controllers
        static int lastNumJoysticks = 0;
        int numJoysticks = SDL_NumJoysticks();
        if (numJoysticks != lastNumJoysticks) {
            // Close old ones
            for (auto c : controllers) {
                SDL_GameControllerClose(c);
            }
            controllers.clear();
            
            // Open new ones
            for (int i = 0; i < numJoysticks; ++i) {
                if (SDL_IsGameController(i)) {
                    SDL_GameController* pad = SDL_GameControllerOpen(i);
                    if (pad) {
                        controllers.insert(i, pad);
                        qDebug() << "Opened Gamepad:" << SDL_GameControllerName(pad);
                    }
                }
            }
            lastNumJoysticks = numJoysticks;
        }

        // Reset gamepad state to Released (1)
        gamepadState = 0xFFFF;

        // Poll buttons
        for (auto pad : controllers) {
            for (auto it = gamepadToButtonMap.begin(); it != gamepadToButtonMap.end(); ++it) {
                int sdlBtn = it.key();
                GBAButton gbaBtn = it.value();
                if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)sdlBtn)) {
                    gamepadState &= ~(1 << gbaBtn); // Pressed (0)
                }
            }
        }

        uint16_t result = (keyboardState & gamepadState) & 0x03FF; // Only lower 10 bits are valid for KEYINPUT
        
        return result;
    }

    void InputManager::setMapping(int qtKey, GBAButton button) {
        if (buttonToKeyMap.contains(button)) {
            int oldKey = buttonToKeyMap[button];
            keyToButtonMap.remove(oldKey);
        }
        if (keyToButtonMap.contains(qtKey)) {
            GBAButton oldBtn = keyToButtonMap[qtKey];
            buttonToKeyMap.remove(oldBtn);
        }
        keyToButtonMap[qtKey] = button;
        buttonToKeyMap[button] = qtKey;
    }

    int InputManager::getKeyForButton(GBAButton button) const {
        return buttonToKeyMap.value(button, Qt::Key_unknown);
    }

    void InputManager::setGamepadMapping(int sdlButton, GBAButton button) {
        if (buttonToGamepadMap.contains(button)) {
            int oldBtn = buttonToGamepadMap[button];
            gamepadToButtonMap.remove(oldBtn);
        }
        if (gamepadToButtonMap.contains(sdlButton)) {
            GBAButton oldGbaBtn = gamepadToButtonMap[sdlButton];
            buttonToGamepadMap.remove(oldGbaBtn);
        }
        gamepadToButtonMap[sdlButton] = button;
        buttonToGamepadMap[button] = sdlButton;
    }

    int InputManager::getGamepadButtonForButton(GBAButton button) const {
        return buttonToGamepadMap.value(button, SDL_CONTROLLER_BUTTON_INVALID);
    }

    QString InputManager::getButtonName(GBAButton button) const {
        switch(button) {
            case Button_A: return "A";
            case Button_B: return "B";
            case Button_Select: return "Select";
            case Button_Start: return "Start";
            case Button_Right: return "Right";
            case Button_Left: return "Left";
            case Button_Up: return "Up";
            case Button_Down: return "Down";
            case Button_R: return "R";
            case Button_L: return "L";
            default: return "Unknown";
        }
    }

    QString InputManager::getGamepadButtonName(int sdlButton) const {
        return QString(SDL_GameControllerGetStringForButton((SDL_GameControllerButton)sdlButton));
    }

    void InputManager::loadConfig() {
        QSettings settings("AIO", "Server");
        settings.beginGroup("Input");
        
        if (settings.childKeys().isEmpty()) {
            qDebug() << "No saved input config, using defaults";
            // Keyboard Defaults
            setMapping(Qt::Key_Z, Button_A);
            setMapping(Qt::Key_X, Button_B);
            setMapping(Qt::Key_Backspace, Button_Select);
            setMapping(Qt::Key_Return, Button_Start);
            setMapping(Qt::Key_Right, Button_Right);
            setMapping(Qt::Key_Left, Button_Left);
            setMapping(Qt::Key_Up, Button_Up);
            setMapping(Qt::Key_Down, Button_Down);
            setMapping(Qt::Key_S, Button_R);
            setMapping(Qt::Key_A, Button_L);
            
            qDebug() << "Mapped Z (key" << Qt::Key_Z << ") to A button";
            qDebug() << "Mapped Return (key" << Qt::Key_Return << ") to Start button";

            // Gamepad Defaults
            setGamepadMapping(SDL_CONTROLLER_BUTTON_B, Button_A);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_A, Button_B);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_BACK, Button_Select);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_START, Button_Start);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_DPAD_UP, Button_Up);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_DPAD_DOWN, Button_Down);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_DPAD_LEFT, Button_Left);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Button_Right);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Button_R);
            setGamepadMapping(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Button_L);

        } else {
            for (int i = 0; i < Button_Count; ++i) {
                GBAButton btn = static_cast<GBAButton>(i);
                QString name = getButtonName(btn);
                
                int key = settings.value(name + "_Key", Qt::Key_unknown).toInt();
                if (key != Qt::Key_unknown) setMapping(key, btn);

                int gpBtn = settings.value(name + "_Gamepad", SDL_CONTROLLER_BUTTON_INVALID).toInt();
                if (gpBtn != SDL_CONTROLLER_BUTTON_INVALID) setGamepadMapping(gpBtn, btn);
            }
        }
        settings.endGroup();
    }

    void InputManager::saveConfig() {
        QSettings settings("AIO", "Server");
        settings.beginGroup("Input");
        for (int i = 0; i < Button_Count; ++i) {
            GBAButton btn = static_cast<GBAButton>(i);
            QString name = getButtonName(btn);
            settings.setValue(name + "_Key", getKeyForButton(btn));
            settings.setValue(name + "_Gamepad", getGamepadButtonForButton(btn));
        }
        settings.endGroup();
    }

}
