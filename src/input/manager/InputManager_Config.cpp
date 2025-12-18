#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QSettings>

namespace AIO::Input {

void InputManager::loadConfig() {
    QSettings settings("AIO", "Server");

    // App-wide UI bindings
    settings.beginGroup("Input/UI");

    if (!settings.childKeys().isEmpty()) {
        auto loadUi = [&](LogicalButton logical, const char* keyName, int defaultKey) {
            const int k = settings.value(QString::fromUtf8(keyName), defaultKey).toInt();
            if (k != Qt::Key_unknown) setUIKeyBinding(logical, k);
        };

        loadUi(LogicalButton::Confirm, "Confirm_Key", Qt::Key_Return);
        loadUi(LogicalButton::Back, "Back_Key", Qt::Key_Escape);
        loadUi(LogicalButton::Up, "Up_Key", Qt::Key_Up);
        loadUi(LogicalButton::Down, "Down_Key", Qt::Key_Down);
        loadUi(LogicalButton::Left, "Left_Key", Qt::Key_Left);
        loadUi(LogicalButton::Right, "Right_Key", Qt::Key_Right);
        loadUi(LogicalButton::Home, "Home_Key", Qt::Key_Home);
    }
    settings.endGroup();

    // Per-emulator (GBA) gameplay bindings
    settings.beginGroup("Input/GBA");

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

        // Gamepad defaults (per-family, updated when a controller is opened)
        detail::applyDefaultMappingsForFamily(this, activeFamily_);

    } else {
        for (int i = 0; i < Button_Count; ++i) {
            const GBAButton btn = static_cast<GBAButton>(i);
            const QString name = getButtonName(btn);

            const int key = settings.value(name + "_Key", Qt::Key_unknown).toInt();
            if (key != Qt::Key_unknown) setMapping(key, btn);

            const int gpBtn = settings.value(name + "_Gamepad", SDL_CONTROLLER_BUTTON_INVALID).toInt();
            if (gpBtn != SDL_CONTROLLER_BUTTON_INVALID) setGamepadMapping(gpBtn, btn);
        }
    }
    settings.endGroup();

    // Backward compatibility: if legacy group exists, still load it (but don't overwrite UI).
    settings.beginGroup("Input");
    if (!settings.childKeys().isEmpty()) {
        for (int i = 0; i < Button_Count; ++i) {
            const GBAButton btn = static_cast<GBAButton>(i);
            const QString name = getButtonName(btn);

            const int key = settings.value(name + "_Key", Qt::Key_unknown).toInt();
            if (key != Qt::Key_unknown) setMapping(key, btn);

            const int gpBtn = settings.value(name + "_Gamepad", SDL_CONTROLLER_BUTTON_INVALID).toInt();
            if (gpBtn != SDL_CONTROLLER_BUTTON_INVALID) setGamepadMapping(gpBtn, btn);
        }
    }
    settings.endGroup();
}

void InputManager::saveConfig() {
    QSettings settings("AIO", "Server");

    // App-wide UI bindings
    settings.beginGroup("Input/UI");
    settings.setValue("Confirm_Key", uiKeyBinding(LogicalButton::Confirm));
    settings.setValue("Back_Key", uiKeyBinding(LogicalButton::Back));
    settings.setValue("Up_Key", uiKeyBinding(LogicalButton::Up));
    settings.setValue("Down_Key", uiKeyBinding(LogicalButton::Down));
    settings.setValue("Left_Key", uiKeyBinding(LogicalButton::Left));
    settings.setValue("Right_Key", uiKeyBinding(LogicalButton::Right));
    settings.setValue("Home_Key", uiKeyBinding(LogicalButton::Home));
    settings.endGroup();

    // Per-emulator (GBA) bindings
    settings.beginGroup("Input/GBA");
    for (int i = 0; i < Button_Count; ++i) {
        const GBAButton btn = static_cast<GBAButton>(i);
        const QString name = getButtonName(btn);
        settings.setValue(name + "_Key", getKeyForButton(btn));
        settings.setValue(name + "_Gamepad", getGamepadButtonForButton(btn));
    }
    settings.endGroup();
}

} // namespace AIO::Input
