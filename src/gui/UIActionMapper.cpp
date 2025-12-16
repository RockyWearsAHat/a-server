#include "gui/UIActionMapper.h"

#include "input/InputManager.h"

namespace AIO::GUI {

UIActionMapper::UIActionMapper() {
    timer_.start();
}

bool UIActionMapper::pressed(uint16_t state, int bit) const {
    return (state & (1u << bit)) == 0;
}

bool UIActionMapper::edgePressed(uint16_t state, int bit) const {
    const bool isDown = pressed(state, bit);
    const bool wasDown = (lastState_ & (1u << bit)) == 0;
    return isDown && !wasDown;
}

void UIActionMapper::notifyMouseActivity() {
    lastSource_ = UIInputSource::Mouse;
    lastMouseMs_ = timer_.elapsed();
}

UIActionFrame UIActionMapper::update(uint16_t inputState) {
    const qint64 nowMs = timer_.elapsed();

    auto logicalPressed = [&](AIO::Input::LogicalButton b) {
        const uint32_t mask = 1u << static_cast<uint32_t>(b);
        return (AIO::Input::InputManager::instance().logicalButtonsDown() & mask) == 0;
    };

    auto logicalEdge = [&](AIO::Input::LogicalButton b) {
        const uint32_t mask = 1u << static_cast<uint32_t>(b);
        const bool isDown = logicalPressed(b);
        const bool wasDown = (lastLogicalButtons_ & mask) == 0;
        return isDown && !wasDown;
    };

    auto setSourceIfAnyDown = [&]() {
        // Any controller/keyboard activity will override mouse mode.
        // UI is driven by logical buttons (not emulator bindings).
        if (logicalPressed(AIO::Input::LogicalButton::Up) || logicalPressed(AIO::Input::LogicalButton::Down) ||
            logicalPressed(AIO::Input::LogicalButton::Left) || logicalPressed(AIO::Input::LogicalButton::Right) ||
            logicalPressed(AIO::Input::LogicalButton::Confirm) || logicalPressed(AIO::Input::LogicalButton::Back)) {
            if (lastSource_ != UIInputSource::Controller) {
                lastSource_ = UIInputSource::Controller;
            }
        }
    };

    setSourceIfAnyDown();

    // System/Home button (Guide on Xbox, PS on PlayStation, Home on Switch controllers).
    // This is not part of GBA KEYINPUT, so read it from InputManager.
    if (logicalPressed(AIO::Input::LogicalButton::Home) || (AIO::Input::InputManager::instance().systemButtonsDown() & 0x1u) != 0) {
        lastSource_ = UIInputSource::Controller;
        lastState_ = inputState;
        return {UIAction::Home, lastSource_};
    }

    // Directional repeat.
    constexpr qint64 INITIAL_DELAY_MS = 400;  // Longer initial delay to prevent double inputs
    constexpr qint64 REPEAT_MS = 150;  // Slower repeats overall

    auto startRepeat = [&](int bit) {
        repeatBit_ = bit;
        nextRepeatMs_ = nowMs + INITIAL_DELAY_MS;
    };

    auto repeatReady = [&](int bit) {
        return repeatBit_ == bit && nowMs >= nextRepeatMs_;
    };

    auto bumpRepeat = [&]() { nextRepeatMs_ = nowMs + REPEAT_MS; };

    // Prefer edges first (snappy), then repeat.
    struct DirMap { AIO::Input::LogicalButton logical; int repeatKey; UIAction act; };
    const DirMap dirs[] = {
        {AIO::Input::LogicalButton::Up, (int)AIO::Input::LogicalButton::Up, UIAction::Up},
        {AIO::Input::LogicalButton::Down, (int)AIO::Input::LogicalButton::Down, UIAction::Down},
        {AIO::Input::LogicalButton::Left, (int)AIO::Input::LogicalButton::Left, UIAction::Left},
        {AIO::Input::LogicalButton::Right, (int)AIO::Input::LogicalButton::Right, UIAction::Right},
    };

    for (const auto& d : dirs) {
        if (logicalEdge(d.logical)) {
            startRepeat(d.repeatKey);
            lastLogicalButtons_ = AIO::Input::InputManager::instance().logicalButtonsDown();
            lastState_ = inputState;
            return {d.act, lastSource_};
        }
    }

    for (const auto& d : dirs) {
        if (logicalPressed(d.logical)) {
            if (repeatBit_ != d.repeatKey) startRepeat(d.repeatKey);
            if (repeatReady(d.repeatKey)) {
                bumpRepeat();
                lastLogicalButtons_ = AIO::Input::InputManager::instance().logicalButtonsDown();
                lastState_ = inputState;
                return {d.act, lastSource_};
            }
        }
    }

    // Stop repeat when no directions held.
    if (!logicalPressed(AIO::Input::LogicalButton::Up) && !logicalPressed(AIO::Input::LogicalButton::Down) &&
        !logicalPressed(AIO::Input::LogicalButton::Left) && !logicalPressed(AIO::Input::LogicalButton::Right)) {
        repeatBit_ = -1;
    }

    if (logicalEdge(AIO::Input::LogicalButton::Confirm)) {
        lastLogicalButtons_ = AIO::Input::InputManager::instance().logicalButtonsDown();
        lastState_ = inputState;
        return {UIAction::Select, lastSource_};
    }

    if (logicalEdge(AIO::Input::LogicalButton::Back)) {
        lastLogicalButtons_ = AIO::Input::InputManager::instance().logicalButtonsDown();
        lastState_ = inputState;
        return {UIAction::Back, lastSource_};
    }

    lastLogicalButtons_ = AIO::Input::InputManager::instance().logicalButtonsDown();
    lastState_ = inputState;
    return {UIAction::None, lastSource_};
}

} // namespace AIO::GUI
