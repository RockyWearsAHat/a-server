#include "gui/NavigationController.h"
#include "gui/ButtonListAdapter.h"

#include <algorithm>
#include <iostream>
#include <typeinfo>

namespace AIO::GUI {

void NavigationController::setAdapter(NavigationAdapter* adapter) {
    adapter_ = adapter;
    pageWidget_ = adapter_ ? adapter_->pageWidget() : nullptr;
    hoveredIndex_ = -1;
    if (adapter_ && adapter_->itemCount() > 0) {
        adapter_->setHoveredIndex(-1);
    }
}

void NavigationController::clearHover() {
    hoveredIndex_ = -1;
    if (adapter_) adapter_->setHoveredIndex(-1);
}

void NavigationController::setHoverFromMouse(int index) {
    if (!adapter_) return;
    const int n = adapter_->itemCount();
    if (n <= 0) return;
    
    // If index is -1, clear mouse hover
    if (index < 0) {
        adapter_->clearMouseHover();
        return;
    }
    
    // Set mouse hover index (this overrides visual display but doesn't change controller selection)
    int clampedIndex = std::clamp(index, 0, n - 1);
    adapter_->setMouseHoverIndex(clampedIndex);
}

void NavigationController::setControllerSelection(int index) {
    if (!adapter_) return;
    const int n = adapter_->itemCount();
    if (n <= 0) {
        hoveredIndex_ = -1;
        return;
    }
    
    hoveredIndex_ = std::clamp(index, 0, n - 1);
    clampAndApplyHover();
}

void NavigationController::clampAndApplyHover() {
    if (!adapter_) return;
    const int n = adapter_->itemCount();
    if (n <= 0) {
        hoveredIndex_ = -1;
        adapter_->setHoveredIndex(-1);
        return;
    }

    hoveredIndex_ = std::clamp(hoveredIndex_, 0, n - 1);
    
    // Prefer controller-navigation path only for true button-list pages.
    // Some adapters (e.g., ROM list) may inherit ButtonListAdapter for shared styling,
    // but they override setHoveredIndex/activateIndex to drive a QListWidget instead.
    // In those cases, calling setHoveredIndex() is the correct behavior.
    if (auto* buttonList = dynamic_cast<AIO::GUI::ButtonListAdapter*>(adapter_)) {
        // If the adapter is exactly ButtonListAdapter, use its controller-aware path.
        // If it's a subclass, call setHoveredIndex() so the subclass can map the index
        // to its own model (e.g., QListWidget rows).
        if (typeid(*buttonList) == typeid(AIO::GUI::ButtonListAdapter)) {
            buttonList->onControllerNavigation(hoveredIndex_);
            return;
        }
    }

    adapter_->setHoveredIndex(hoveredIndex_);
}

bool NavigationController::apply(const UIActionFrame& frame) {
    if (!adapter_) return false;

    const int n = adapter_->itemCount();
    if (n <= 0) return false;

    // First, clear any mouse hover since controller input is being used
    adapter_->clearMouseHover();

    // First directional press selects first item.
    const bool isDirectional = (frame.primary == UIAction::Up || frame.primary == UIAction::Down ||
                                frame.primary == UIAction::Left || frame.primary == UIAction::Right);
    if (hoveredIndex_ < 0 && isDirectional) {
        hoveredIndex_ = 0;
        clampAndApplyHover();
        return true;
    }

    switch (frame.primary) {
        case UIAction::Up:
        case UIAction::Left:
            hoveredIndex_ = (hoveredIndex_ <= 0) ? (n - 1) : (hoveredIndex_ - 1);
            clampAndApplyHover();
            return true;

        case UIAction::Down:
        case UIAction::Right:
            hoveredIndex_ = (hoveredIndex_ >= n - 1) ? 0 : (hoveredIndex_ + 1);
            clampAndApplyHover();
            return true;

        case UIAction::Select:
            if (hoveredIndex_ < 0) hoveredIndex_ = 0;
            adapter_->activateIndex(hoveredIndex_);
            return true;

        case UIAction::Back:
            return adapter_->back();

        case UIAction::Home:
            // Let the owning window decide global routing; returning false allows upstream handling.
            return false;

        case UIAction::None:
        default:
            return false;
    }
}

} // namespace AIO::GUI
