#pragma once

#include "gui/NavigationAdapter.h"

#include <QElapsedTimer>

namespace AIO::GUI {

// Produces edge-triggered UI actions from merged KEYINPUT-style bitfield.
class UIActionMapper {
public:
    UIActionMapper();

    // Update with current merged input state (active-low bits like KEYINPUT).
    // Returns at most one primary action per tick.
    UIActionFrame update(uint16_t inputState);

    // Inform mapper that mouse moved/clicked so we can switch cursor mode.
    void notifyMouseActivity();

    UIInputSource lastSource() const { return lastSource_; }

private:
    bool pressed(uint16_t state, int bit) const;
    bool edgePressed(uint16_t state, int bit) const;

    uint16_t lastState_ = 0x03FF;
    uint32_t lastLogicalButtons_ = 0xFFFFFFFFu;
    QElapsedTimer timer_;

    // Simple repeat for directional input.
    qint64 nextRepeatMs_ = 0;
    int repeatBit_ = -1;

    UIInputSource lastSource_ = UIInputSource::Unknown;
    qint64 lastMouseMs_ = -1;
};

} // namespace AIO::GUI
