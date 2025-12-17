#include <gtest/gtest.h>

#include "input/InputTypes.h"

namespace {

static constexpr uint32_t mask(AIO::Input::LogicalButton b) {
    return 1u << static_cast<uint32_t>(b);
}

// Models the intended high-level behavior of InputManager's per-frame merge:
// controller-backed logical bits are defaulted to released each frame, then
// pressed bits are applied from the current controller state.
static uint32_t mergeLogical(uint32_t previousLogical, uint32_t controllerPressedMask) {
    // 1=released, 0=pressed (active-low)
    uint32_t merged = previousLogical;

    const uint32_t controllerLogicalMask =
        mask(AIO::Input::LogicalButton::Confirm) |
        mask(AIO::Input::LogicalButton::Back) |
        mask(AIO::Input::LogicalButton::Aux1) |
        mask(AIO::Input::LogicalButton::Aux2) |
        mask(AIO::Input::LogicalButton::Select) |
        mask(AIO::Input::LogicalButton::Start) |
        mask(AIO::Input::LogicalButton::L) |
        mask(AIO::Input::LogicalButton::R) |
        mask(AIO::Input::LogicalButton::Home);

    // Default controller-backed bits to released.
    merged |= controllerLogicalMask;

    // Apply current controller presses.
    merged &= ~controllerPressedMask;

    return merged;
}

} // namespace

TEST(InputLogic, ControllerButtonsDoNotLatchAcrossFrames) {
    const uint32_t start = 0xFFFFFFFFu;
    const uint32_t confirmMask = mask(AIO::Input::LogicalButton::Confirm);
    const uint32_t backMask = mask(AIO::Input::LogicalButton::Back);

    // Frame 1: Confirm pressed.
    uint32_t logical = mergeLogical(start, confirmMask);
    EXPECT_TRUE((logical & confirmMask) == 0u);
    EXPECT_TRUE((logical & backMask) != 0u);

    // Frame 2: controller releases all buttons.
    logical = mergeLogical(logical, 0u);
    EXPECT_TRUE((logical & confirmMask) != 0u);
    EXPECT_TRUE((logical & backMask) != 0u);

    // Frame 3: Back pressed.
    logical = mergeLogical(logical, backMask);
    EXPECT_TRUE((logical & confirmMask) != 0u);
    EXPECT_TRUE((logical & backMask) == 0u);
}
