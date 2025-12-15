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

    auto setSourceIfAnyDown = [&]() {
        // Any controller/keyboard activity will override mouse mode.
        // We only have merged state, so treat any pressed bit as non-mouse.
        if ((inputState & 0x03FF) != 0x03FF) {
            if (lastSource_ != UIInputSource::Controller) {
                // In our mapping, controller and keyboard are merged; assume controller
                // if a gamepad is present, otherwise keyboard. This is good enough for cursor hiding.
                lastSource_ = UIInputSource::Controller;
            }
        }
    };

    setSourceIfAnyDown();

    // Directional repeat.
    constexpr qint64 INITIAL_DELAY_MS = 220;
    constexpr qint64 REPEAT_MS = 70;

    auto startRepeat = [&](int bit) {
        repeatBit_ = bit;
        nextRepeatMs_ = nowMs + INITIAL_DELAY_MS;
    };

    auto repeatReady = [&](int bit) {
        return repeatBit_ == bit && nowMs >= nextRepeatMs_;
    };

    auto bumpRepeat = [&]() { nextRepeatMs_ = nowMs + REPEAT_MS; };

    // Prefer edges first (snappy), then repeat.
    struct DirMap { int bit; UIAction act; };
    const DirMap dirs[] = {
        {AIO::Input::Button_Up, UIAction::Up},
        {AIO::Input::Button_Down, UIAction::Down},
        {AIO::Input::Button_Left, UIAction::Left},
        {AIO::Input::Button_Right, UIAction::Right},
    };

    for (const auto& d : dirs) {
        if (edgePressed(inputState, d.bit)) {
            startRepeat(d.bit);
            lastState_ = inputState;
            return {d.act, lastSource_};
        }
    }

    for (const auto& d : dirs) {
        if (pressed(inputState, d.bit)) {
            if (repeatBit_ != d.bit) startRepeat(d.bit);
            if (repeatReady(d.bit)) {
                bumpRepeat();
                lastState_ = inputState;
                return {d.act, lastSource_};
            }
        }
    }

    // Stop repeat when no directions held.
    if (!pressed(inputState, AIO::Input::Button_Up) && !pressed(inputState, AIO::Input::Button_Down) &&
        !pressed(inputState, AIO::Input::Button_Left) && !pressed(inputState, AIO::Input::Button_Right)) {
        repeatBit_ = -1;
    }

    if (edgePressed(inputState, AIO::Input::Button_A)) {
        lastState_ = inputState;
        return {UIAction::Select, lastSource_};
    }

    if (edgePressed(inputState, AIO::Input::Button_B)) {
        lastState_ = inputState;
        return {UIAction::Back, lastSource_};
    }

    lastState_ = inputState;
    return {UIAction::None, lastSource_};
}

} // namespace AIO::GUI
