#include "input/InputManager.h"
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <iostream>

#include "input/ActionBindings.h"

namespace {
static uint32_t logicalMaskFor(AIO::Input::LogicalButton b) {
    return 1u << static_cast<uint32_t>(b);
}

static SDL_GameControllerButton sdlButtonFromName(const QString& name) {
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

static const MappingEntry kMappingXboxLike[] = {
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

static const MappingEntry kMappingNintendo[] = {
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

static const FamilyMapping kFamilyMappings[] = {
    {AIO::Input::ControllerFamily::Nintendo, "Nintendo", kMappingNintendo, sizeof(kMappingNintendo) / sizeof(kMappingNintendo[0])},
    {AIO::Input::ControllerFamily::Xbox, "Xbox", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::PlayStation, "PlayStation", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::Generic, "Generic", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
    {AIO::Input::ControllerFamily::Unknown, "Unknown", kMappingXboxLike, sizeof(kMappingXboxLike) / sizeof(kMappingXboxLike[0])},
};

static AIO::Input::ControllerFamily detectFamilyFromName(const QString& name) {
    const QString n = name.toLower();
    if (n.contains("xbox") || n.contains("xinput") || n.contains("microsoft")) {
        return AIO::Input::ControllerFamily::Xbox;
    }
    if (n.contains("dualshock") || n.contains("dualsense") || n.contains("playstation") || n.contains("ps4") || n.contains("ps5") || n.contains("sony")) {
        return AIO::Input::ControllerFamily::PlayStation;
    }
    if (n.contains("nintendo") || n.contains("switch") || n.contains("joy-con") || n.contains("pro controller")) {
        return AIO::Input::ControllerFamily::Nintendo;
    }
    if (n.isEmpty()) return AIO::Input::ControllerFamily::Unknown;
    return AIO::Input::ControllerFamily::Generic;
}

static void applyDefaultMappingsForFamily(AIO::Input::InputManager* mgr, AIO::Input::ControllerFamily fam) {
    for (const auto& fm : kFamilyMappings) {
        if (fm.family != fam) continue;
        for (size_t i = 0; i < fm.entryCount; ++i) {
            mgr->setGamepadMapping((int)fm.entries[i].sdl, fm.entries[i].gba);
        }
        return;
    }
}
} // namespace

namespace AIO {
namespace Input {

    namespace {
    static bool gAioInputDebug = false;
    }

    InputManager& InputManager::instance() {
        static InputManager _instance;
        return _instance;
    }

    InputManager::InputManager() {
        // Only initialize the GameController subsystem here.
        // SDL audio is initialized/owned by MainWindow; calling SDL_Quit() from this
        // singleton would shut down audio and other SDL subsystems globally.
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
            qWarning() << "SDL could not initialize! SDL Error:" << SDL_GetError();
        }

        // Enable with: export AIO_INPUT_DEBUG=1
        gAioInputDebug = (qEnvironmentVariableIntValue("AIO_INPUT_DEBUG") != 0);

        loadControllerMappingRegistry();

        // Default logical -> GBA bindings (can be changed per emulator later).
        setGBALogicalBinding(LogicalButton::Confirm, Button_A);
        setGBALogicalBinding(LogicalButton::Back, Button_B);
        setGBALogicalBinding(LogicalButton::Select, Button_Select);
        setGBALogicalBinding(LogicalButton::Start, Button_Start);
        setGBALogicalBinding(LogicalButton::L, Button_L);
        setGBALogicalBinding(LogicalButton::R, Button_R);
        setGBALogicalBinding(LogicalButton::Up, Button_Up);
        setGBALogicalBinding(LogicalButton::Down, Button_Down);
        setGBALogicalBinding(LogicalButton::Left, Button_Left);
        setGBALogicalBinding(LogicalButton::Right, Button_Right);

        // App-wide UI keyboard defaults (10-foot UI expectations).
        setUIKeyBinding(LogicalButton::Confirm, Qt::Key_Return);
        setUIKeyBinding(LogicalButton::Back, Qt::Key_Escape);
        setUIKeyBinding(LogicalButton::Up, Qt::Key_Up);
        setUIKeyBinding(LogicalButton::Down, Qt::Key_Down);
        setUIKeyBinding(LogicalButton::Left, Qt::Key_Left);
        setUIKeyBinding(LogicalButton::Right, Qt::Key_Right);
        setUIKeyBinding(LogicalButton::Home, Qt::Key_Home);
        loadConfig();
    }

    InputManager::~InputManager() {
        saveConfig();
        for (auto c : controllers) {
            SDL_GameControllerClose(c);
        }
        // Do not call SDL_Quit() here; other parts of the app (audio) may still be using SDL.
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }

    bool InputManager::processKeyEvent(QKeyEvent* event) {
        int key = event->key();

        // First: app-wide UI logical bindings.
        if (uiKeyToLogical_.contains(key)) {
            const LogicalButton logical = uiKeyToLogical_.value(key);
            const uint32_t mask = 1u << static_cast<uint32_t>(logical);

            if (event->type() == QEvent::KeyPress) {
                logicalButtonsDown_ &= ~mask; // pressed
            } else if (event->type() == QEvent::KeyRelease) {
                if (!event->isAutoRepeat()) {
                    logicalButtonsDown_ |= mask; // released
                }
            }
            return true;
        }
        
        if (!keyToButtonMap.contains(key)) {
            return false;
        }

        GBAButton btn = keyToButtonMap[key];
        int bit = static_cast<int>(btn);

        if (event->type() == QEvent::KeyPress) {
            // 0 = Pressed
            keyboardState &= ~(1 << bit);
        } else if (event->type() == QEvent::KeyRelease) {
            // 1 = Released
            if (!event->isAutoRepeat()) {
                keyboardState |= (1 << bit);
            }
        }

        return true;
    }

    void InputManager::setUIKeyBinding(LogicalButton logical, int qtKey) {
        if (uiLogicalToKey_.contains(logical)) {
            const int oldKey = uiLogicalToKey_.value(logical);
            uiKeyToLogical_.remove(oldKey);
        }
        if (uiKeyToLogical_.contains(qtKey)) {
            const LogicalButton oldLogical = uiKeyToLogical_.value(qtKey);
            uiLogicalToKey_.remove(oldLogical);
        }
        uiKeyToLogical_[qtKey] = logical;
        uiLogicalToKey_[logical] = qtKey;
    }

    int InputManager::uiKeyBinding(LogicalButton logical) const {
        return uiLogicalToKey_.value(logical, Qt::Key_unknown);
    }

    void InputManager::setLogicalMapping(LogicalButton logical, int sdlButton) {
        for (auto it = sdlToLogical_.begin(); it != sdlToLogical_.end(); ) {
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
            applyDefaultMappingsForFamily(this, activeFamily_);
            return;
        }

        const QJsonArray controllersJson = controllerRegistryDoc_.value("controllers").toArray();
        if (controllersJson.isEmpty()) {
            applyDefaultMappingsForFamily(this, activeFamily_);
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
            applyDefaultMappingsForFamily(this, activeFamily_);
            return;
        }

        const QJsonObject layout = best.value("layout").toObject();
        auto map = [&](LogicalButton logical, const char* key) {
            const QString btnName = layout.value(key).toString();
            const SDL_GameControllerButton btn = sdlButtonFromName(btnName);
            setLogicalMapping(logical, (int)btn);
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

    uint16_t InputManager::update() {
        // Capture previous logical state for edge detection.
        lastLogicalButtonsDown_ = logicalButtonsDown_;

        // Drain the SDL event queue so OS-level controller shortcuts (Guide/Home)
        // don't escape into default platform behaviors while our window is focused.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Intentionally consume controller events.
            // We read current state via SDL_GameControllerGet* below.
        }

        SDL_GameControllerUpdate();

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
                        const QString name = QString::fromUtf8(SDL_GameControllerName(pad));
                        qDebug() << "Opened Gamepad:" << name;

                        // Pick an "active" controller for mapping decisions (first one opened).
                        if (activeControllerName_.isEmpty()) {
                            activeControllerName_ = name;
                            activeFamily_ = detectFamilyFromName(name);
                            // Apply best layout from JSON registry when controller changes.
                            applyBestControllerLayoutForActivePad();
                        }
                    }
                }
            }
            lastNumJoysticks = numJoysticks;
        }

        // Update system buttons (Guide/Home/PS) separately from emulation input.
        systemButtonsDown_ = 0;

        // Start from keyboard-driven logical state (maintained in processKeyEvent).
        // We'll merge controller-derived logical presses into this.
        uint32_t mergedLogical = logicalButtonsDown_;

        // Direction comes from keyboard OR controller, but controller direction must be
        // recomputed every frame. Clear the UI direction bits here so they don't latch.
        mergedLogical |= logicalMaskFor(LogicalButton::Up);
        mergedLogical |= logicalMaskFor(LogicalButton::Down);
        mergedLogical |= logicalMaskFor(LogicalButton::Left);
        mergedLogical |= logicalMaskFor(LogicalButton::Right);

        // Reset gamepad state to Released (1)
        gamepadState = 0xFFFF;

        // Poll buttons
        for (auto pad : controllers) {
            if (gAioInputDebug) {
                const int du = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
                const int dd = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                const int dl = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                const int dr = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

                const int lx = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
                const int ly = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
                const int rx = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
                const int ry = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);

                qDebug() << "[INPUT] dpad" << du << dd << dl << dr
                         << "axes" << lx << ly << rx << ry
                         << "name" << activeControllerName_;
            }
            if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_GUIDE)) {
                systemButtonsDown_ |= 0x1u; // Home/Guide
            }

            // Logical buttons (global mapping)
            for (auto it = sdlToLogical_.begin(); it != sdlToLogical_.end(); ++it) {
                const int sdlBtn = it.key();
                const LogicalButton logical = it.value();
                if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)sdlBtn)) {
                    mergedLogical &= ~logicalMaskFor(logical);
                }
            }

            // --- UI Direction Provider ---
            // Merge D-pad + both sticks into ONE stable direction, and prefer the
            // most recent source when both are used.
            enum class Dir { None, Up, Down, Left, Right };
            enum class DirSource { None, Dpad, Stick };

            static DirSource lastSource = DirSource::None;
            static qint64 lastSourceMs = 0;
            static QElapsedTimer sourceTimer;
            if (!sourceTimer.isValid()) sourceTimer.start();

            static Dir lastDpadDir = Dir::None;
            static Dir lastStickDir = Dir::None;
            static Dir lastChosenDir = Dir::None;
            static DirSource lastLoggedSource = DirSource::None;

            auto collapseToSingle = [&](bool up, bool down, bool left, bool right) -> Dir {
                // Clear impossible opposites.
                if (up && down) { up = false; down = false; }
                if (left && right) { left = false; right = false; }

                // Collapse diagonals to a single axis. Prefer vertical (menus feel better).
                if ((up || down) && (left || right)) {
                    left = false;
                    right = false;
                }

                if (up) return Dir::Up;
                if (down) return Dir::Down;
                if (left) return Dir::Left;
                if (right) return Dir::Right;
                return Dir::None;
            };

            // D-pad intent
            bool dpadUp = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
            bool dpadDown = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            bool dpadLeft = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            bool dpadRight = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            const Dir dpadDir = collapseToSingle(dpadUp, dpadDown, dpadLeft, dpadRight);

            // Stick intent (both sticks)
            QSettings uiSettings("AIO", "Server");
            uiSettings.beginGroup("Input/UI");
            const int pressDeadzone = uiSettings.value("UIStickPressDeadzone", 20000).toInt();
            const int releaseDeadzone = uiSettings.value("UIStickReleaseDeadzone", 16000).toInt();
            uiSettings.endGroup();

            const int lx = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            const int ly = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            const int rx = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
            const int ry = (int)SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);

            auto axisToDir = [&](int x, int y) -> Dir {
                // Dominant axis filter: whichever magnitude is larger wins.
                const int ax = std::abs(x);
                const int ay = std::abs(y);
                const bool wantHorizontal = ax > ay;

                bool up = false, down = false, left = false, right = false;

                if (wantHorizontal) {
                    if (x <= -pressDeadzone) left = true;
                    if (x >= pressDeadzone) right = true;
                } else {
                    if (y <= -pressDeadzone) up = true;
                    if (y >= pressDeadzone) down = true;
                }

                // If below release threshold, treat as none.
                if (ax < releaseDeadzone && ay < releaseDeadzone) {
                    return Dir::None;
                }

                return collapseToSingle(up, down, left, right);
            };

            const Dir stickDir = [&]() {
                const Dir l = axisToDir(lx, ly);
                const Dir r = axisToDir(rx, ry);
                // If both sticks request a direction, prefer the one with larger magnitude.
                if (l == Dir::None) return r;
                if (r == Dir::None) return l;
                const int lmag = std::max(std::abs(lx), std::abs(ly));
                const int rmag = std::max(std::abs(rx), std::abs(ry));
                return (rmag > lmag) ? r : l;
            }();

            // Choose a source. If both active, prefer the most recently changed source.
            // Bump recency when that source's direction changes.
            const qint64 nowMs = sourceTimer.elapsed();
            if (dpadDir != lastDpadDir) {
                lastDpadDir = dpadDir;
                if (dpadDir != Dir::None) { lastSource = DirSource::Dpad; lastSourceMs = nowMs; }
            }
            if (stickDir != lastStickDir) {
                lastStickDir = stickDir;
                if (stickDir != Dir::None) { lastSource = DirSource::Stick; lastSourceMs = nowMs; }
            }

            Dir chosen = Dir::None;
            if (stickDir != Dir::None && dpadDir != Dir::None) {
                chosen = (lastSource == DirSource::Stick) ? stickDir : dpadDir;
            } else if (stickDir != Dir::None) {
                chosen = stickDir;
            } else {
                chosen = dpadDir;
            }

            if (gAioInputDebug && (chosen != lastChosenDir || lastSource != lastLoggedSource)) {
                auto dirName = [&](Dir d) -> const char* {
                    switch (d) {
                        case Dir::Up: return "Up";
                        case Dir::Down: return "Down";
                        case Dir::Left: return "Left";
                        case Dir::Right: return "Right";
                        default: return "None";
                    }
                };
                auto sourceName = [&](DirSource s) -> const char* {
                    switch (s) {
                        case DirSource::Dpad: return "Dpad";
                        case DirSource::Stick: return "Stick";
                        default: return "None";
                    }
                };
                qDebug() << "[INPUT] chosen" << dirName(chosen) << "source" << sourceName(lastSource)
                         << "dpad" << dirName(dpadDir) << "stick" << dirName(stickDir)
                         << "ms" << (nowMs - lastSourceMs);
                lastChosenDir = chosen;
                lastLoggedSource = lastSource;
            }

            // Apply chosen direction into UI logical.
            if (chosen == Dir::Up) mergedLogical &= ~logicalMaskFor(LogicalButton::Up);
            if (chosen == Dir::Down) mergedLogical &= ~logicalMaskFor(LogicalButton::Down);
            if (chosen == Dir::Left) mergedLogical &= ~logicalMaskFor(LogicalButton::Left);
            if (chosen == Dir::Right) mergedLogical &= ~logicalMaskFor(LogicalButton::Right);

            for (auto it = gamepadToButtonMap.begin(); it != gamepadToButtonMap.end(); ++it) {
                int sdlBtn = it.key();
                GBAButton gbaBtn = it.value();
                if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)sdlBtn)) {
                    gamepadState &= ~(1 << gbaBtn); // Pressed (0)
                }
            }

            // (Stick directions are now handled by the unified direction provider above.)
        }

        // Bridge logical->GBA for the GBA core.
        for (auto it = logicalToGBA_.begin(); it != logicalToGBA_.end(); ++it) {
            const LogicalButton logical = it.key();
            const GBAButton gbaBtn = it.value();
            if ((mergedLogical & logicalMaskFor(logical)) == 0) {
                gamepadState &= ~(1 << gbaBtn);
            }
        }

        // Publish merged logical state for UI navigation.
        logicalButtonsDown_ = mergedLogical;

        uint16_t result = (keyboardState & gamepadState) & 0x03FF; // Only lower 10 bits are valid for KEYINPUT
        
        return result;
    }

    bool InputManager::pressed(AppId app, ActionId action) const {
        const LogicalButton logical = ActionBindings::resolve(app, action);
        const uint32_t mask = 1u << static_cast<uint32_t>(logical);
        return (logicalButtonsDown_ & mask) == 0;
    }

    bool InputManager::edgePressed(AppId app, ActionId action) const {
        const LogicalButton logical = ActionBindings::resolve(app, action);
        const uint32_t mask = 1u << static_cast<uint32_t>(logical);
        const bool nowDown = (logicalButtonsDown_ & mask) == 0;
        const bool prevDown = (lastLogicalButtonsDown_ & mask) == 0;
        return nowDown && !prevDown;
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
        // App-wide UI bindings
        settings.beginGroup("Input/UI");

        if (!settings.childKeys().isEmpty()) {
            // Load saved UI key bindings.
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

            // Gamepad Defaults (applied per-controller family when a controller is opened)
            applyDefaultMappingsForFamily(this, activeFamily_);

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

        // Backward compatibility: if legacy group exists, still load it (but don't overwrite UI).
        settings.beginGroup("Input");
        if (!settings.childKeys().isEmpty()) {
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
            GBAButton btn = static_cast<GBAButton>(i);
            QString name = getButtonName(btn);
            settings.setValue(name + "_Key", getKeyForButton(btn));
            settings.setValue(name + "_Gamepad", getGamepadButtonForButton(btn));
        }
        settings.endGroup();
    }

} // namespace Input
} // namespace AIO

