#pragma once

#include "gui/NavigationAdapter.h"

#include <QPointer>

namespace AIO::GUI {

class NavigationController {
public:
    void setAdapter(NavigationAdapter* adapter);
    NavigationAdapter* adapter() const { return adapter_; }

    int hoveredIndex() const { return hoveredIndex_; }
    void clearHover();

    // Applies one action (edge-triggered). Returns true if consumed.
    bool apply(const UIActionFrame& frame);

    // Used to set hover from mouse movement (visual only, doesn't affect controller state).
    void setHoverFromMouse(int index);
    
    // Set controller selection directly (for initialization, etc.)
    void setControllerSelection(int index);

private:
    void clampAndApplyHover();

    QPointer<QWidget> pageWidget_;
    NavigationAdapter* adapter_ = nullptr; // non-owning
    int hoveredIndex_ = -1;
};

} // namespace AIO::GUI
