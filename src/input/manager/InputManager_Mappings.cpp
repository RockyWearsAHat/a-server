#include "input/InputManager.h"

#include "input/manager/InputManager_Internal.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <SDL2/SDL.h>

namespace {
SDL_GameControllerButton sdlButtonFromName(const QString& name) {
    const QByteArray utf8 = name.toUtf8();
    return SDL_GameControllerGetButtonFromString(utf8.constData());
}

struct MappingEntry {
    SDL_GameControllerButton sdl;
    AIO::Input::GBAButton gba;
};

struct FamilyMapping {
    AIO::Input::ControllerFamily family;
    const char* label;
    const MappingEntry* entries;
    size_t entryCount;
};

const MappingEntry kMappingXboxLike[] = {
    {SDL_CONTROLLER_BUTTON_A, AIO::Input::Button_A},
    {SDL_CONTROLLER_BUTTON_B, AIO::Input::Button_B},
    {SDL_CONTROLLER_BUTTON_BACK, AIO::Input::Button_Select},
    {SDL_CONTROLLER_BUTTON_START, AIO::Input::Button_Start},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, AIO::Input::Button_Up},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, AIO::Input::Button_Down},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, AIO::Input::Button_Left},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, AIO::Input::Button_Right},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, AIO::Input::Button_R},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, AIO::Input::Button_L},
};

const MappingEntry kMappingNintendo[] = {
    // Swap A/B for Nintendo expectations.
    {SDL_CONTROLLER_BUTTON_A, AIO::Input::Button_A},
    {SDL_CONTROLLER_BUTTON_B, AIO::Input::Button_B},
    {SDL_CONTROLLER_BUTTON_BACK, AIO::Input::Button_Select},
    {SDL_CONTROLLER_BUTTON_START, AIO::Input::Button_Start},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, AIO::Input::Button_Up},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, AIO::Input::Button_Down},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, AIO::Input::Button_Left},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, AIO::Input::Button_Right},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, AIO::Input::Button_R},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, AIO::Input::Button_L},
};

const FamilyMapping kFamilyMappings[] = {
    {AIO::Input::ControllerFamily::Nintendo, "Nintendo", kMappingNintendo, sizeof(kMappingNintendo) / sizeof(kMappingNintendo[0])},
    {AIO::Input::ControllerFamily::Xbox, "Xbox", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::PlayStation, "PlayStation", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::Generic, "Generic", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::Unknown, "Unknown", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
};
} // namespace

namespace AIO::Input::detail {

ControllerFamily detectFamilyFromName(const QString& name) {
    const QString n = name.toLower();
    if (n.contains("xbox") || n.contains("xinput") || n.contains("microsoft")) {
        return ControllerFamily::Xbox;
    }
    if (n.contains("dualshock") || n.contains("dualsense") || n.contains("playstation") || n.contains("ps4") ||
        n.contains("ps5") || n.contains("sony")) {
        return ControllerFamily::PlayStation;
    }
    if (n.contains("nintendo") || n.contains("switch") || n.contains("joy-con") || n.contains("pro controller")) {
        return ControllerFamily::Nintendo;
    }
    if (n.isEmpty()) return ControllerFamily::Unknown;
    return ControllerFamily::Generic;
}

void applyDefaultMappingsForFamily(InputManager* mgr, ControllerFamily fam) {
    for (const auto& fm : kFamilyMappings) {
        if (fm.family != fam) continue;
        for (size_t i = 0; i < fm.entryCount; ++i) {
            mgr->setGamepadMapping(static_cast<int>(fm.entries[i].sdl), fm.entries[i].gba);
        }
        return;
    }
}

} // namespace AIO::Input::detail

namespace AIO::Input {

void InputManager::setLogicalMapping(LogicalButton logical, int sdlButton) {
    for (auto it = sdlToLogical_.begin(); it != sdlToLogical_.end();) {
        if (it.value() == logical) {
            it = sdlToLogical_.erase(it);
        } else {
            ++it;
        }
    }
    if (sdlButton != SDL_CONTROLLER_BUTTON_INVALID) {
        sdlToLogical_[sdlButton] = logical;
    }
}

void InputManager::setGBALogicalBinding(LogicalButton logical, GBAButton gbaButton) {
    logicalToGBA_[logical] = gbaButton;
}

void InputManager::loadControllerMappingRegistry() {
    QFile f(":/assets/controller_mappings.json");
    if (!f.exists()) {
        f.setFileName("assets/controller_mappings.json");
    }

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open controller mappings JSON:" << f.fileName();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        qWarning() << "Controller mappings JSON is not an object:" << f.fileName();
        return;
    }
    controllerRegistryDoc_ = doc.object();
}

void InputManager::applyBestControllerLayoutForActivePad() {
    if (controllerRegistryDoc_.isEmpty()) {
        detail::applyDefaultMappingsForFamily(this, activeFamily_);
        return;
    }

    const QJsonArray controllersJson = controllerRegistryDoc_.value("controllers").toArray();
    if (controllersJson.isEmpty()) {
        detail::applyDefaultMappingsForFamily(this, activeFamily_);
        return;
    }

    const QString name = activeControllerName_;
    int bestPriority = INT_MIN;
    QJsonObject best;

    for (const auto& v : controllersJson) {
        const QJsonObject c = v.toObject();
        const int priority = c.value("priority").toInt(0);
        const QJsonObject match = c.value("match").toObject();

        bool matches = false;
        const QJsonArray any = match.value("nameContainsAny").toArray();
        if (!any.isEmpty()) {
            for (const auto& s : any) {
                const QString needle = s.toString();
                if (!needle.isEmpty() && name.contains(needle, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
            }
        }
        const QString regexStr = match.value("nameRegex").toString();
        if (!regexStr.isEmpty()) {
            const QRegularExpression re(regexStr, QRegularExpression::CaseInsensitiveOption);
            if (re.isValid() && re.match(name).hasMatch()) {
                matches = true;
            }
        }
        if (!matches) continue;

        if (priority > bestPriority) {
            bestPriority = priority;
            best = c;
        }
    }

    if (best.isEmpty()) {
        detail::applyDefaultMappingsForFamily(this, activeFamily_);
        return;
    }

    const QJsonObject layout = best.value("layout").toObject();
    auto map = [&](LogicalButton logical, const char* key) {
        const QString btnName = layout.value(key).toString();
        const SDL_GameControllerButton btn = sdlButtonFromName(btnName);
        setLogicalMapping(logical, static_cast<int>(btn));
    };

    map(LogicalButton::Confirm, "confirm");
    map(LogicalButton::Back, "back");
    map(LogicalButton::Aux1, "aux1");
    map(LogicalButton::Aux2, "aux2");
    map(LogicalButton::Select, "select");
    map(LogicalButton::Start, "start");
    map(LogicalButton::L, "l");
    map(LogicalButton::R, "r");
    map(LogicalButton::Up, "dpadUp");
    map(LogicalButton::Down, "dpadDown");
    map(LogicalButton::Left, "dpadLeft");
    map(LogicalButton::Right, "dpadRight");
    map(LogicalButton::Home, "home");

    qDebug() << "Applied controller layout:" << best.value("id").toString() << "for" << activeControllerName_;
}

void InputManager::setMapping(int qtKey, GBAButton button) {
    if (buttonToKeyMap.contains(button)) {
        const int oldKey = buttonToKeyMap[button];
        keyToButtonMap.remove(oldKey);
    }
    if (keyToButtonMap.contains(qtKey)) {
        const GBAButton oldBtn = keyToButtonMap[qtKey];
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
        const int oldBtn = buttonToGamepadMap[button];
        gamepadToButtonMap.remove(oldBtn);
    }
    if (gamepadToButtonMap.contains(sdlButton)) {
        const GBAButton oldGbaBtn = gamepadToButtonMap[sdlButton];
        buttonToGamepadMap.remove(oldGbaBtn);
    }
    gamepadToButtonMap[sdlButton] = button;
    buttonToGamepadMap[button] = sdlButton;
}

int InputManager::getGamepadButtonForButton(GBAButton button) const {
    return buttonToGamepadMap.value(button, SDL_CONTROLLER_BUTTON_INVALID);
}

QString InputManager::getButtonName(GBAButton button) const {
    switch (button) {
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
    return QString(SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(sdlButton)));
}

} // namespace AIO::Input
