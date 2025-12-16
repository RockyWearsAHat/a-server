#pragma once

#include "gui/NavigationAdapter.h"

#include <QPointer>
#include <QPushButton>
#include <vector>

namespace AIO::GUI {

/**
 * Generic button-based navigation adapter for any menu with QPushButton items.
 * Provides consistent styling, hover state tracking, and controller/mouse/keyboard support.
 */
class ButtonListAdapter : public NavigationAdapter {
public:
    ButtonListAdapter(QWidget* page, const std::vector<QPushButton*>& buttons);

    // NavigationAdapter interface
    QWidget* pageWidget() const override { return page_; }
    int itemCount() const override;
    int indexOfButton(const QPushButton* button) const;
    void setHoveredIndex(int index) override;
    void setMouseHoverIndex(int index) override;
    void clearMouseHover() override;
    void onControllerNavigation(int newIndex);
    void activateIndex(int index) override;
    bool back() override;

    // Get direct access to button list for setup.
    const std::vector<QPointer<QPushButton>>& getButtons() const { return buttons_; }
    
    // Apply the current hover state to buttons (public for initialization)
    void applyHovered();
    
    // Save current controller index before switching to mouse mode
    void saveControllerIndexBeforeMouse();
    
    // Get the last saved index (for resuming controller navigation after mouse)
    // Prioritizes whichever (mouse hover or controller index) was updated most recently
    int getLastResumeIndex() const {
        // If neither has been touched, default to first button
        if (lastMouseHover_ < 0 && lastControllerIndex_ < 0) {
            return 0;
        }
        // If only one is set, use it
        if (lastMouseHover_ < 0) return lastControllerIndex_;
        if (lastControllerIndex_ < 0) return lastMouseHover_;
        
        // Both are set - use the one that was updated most recently
        return (mouseHoverTimestamp_ > controllerIndexTimestamp_) 
            ? lastMouseHover_ 
            : lastControllerIndex_;
    }

private:
    void applyHoveredInternal();

    QPointer<QWidget> page_;
    std::vector<QPointer<QPushButton>> buttons_;
    int hovered_ = -1;  // Logical controller selection
    int mouseHover_ = -1;  // Mouse hover (for resume tracking only)

    // Single visual selection index that drives styling.
    // If mouseHover_ >= 0, this becomes mouseHover_; otherwise it is hovered_.
    int visualSelected_ = -1;

    int lastMouseHover_ = -1;  // Last position mouse was hovering (for resuming controller)
    int lastControllerIndex_ = -1;  // Last controller index before switching to mouse
    int mouseHoverTimestamp_ = 0;  // When lastMouseHover_ was set
    int controllerIndexTimestamp_ = 0;  // When lastControllerIndex_ was set

    // Tracks the most recent display index that was applied (mouse hover or controller hover).
    // Used to detect the initial controller selection after entering a page (often -1 -> 0).
    int lastAppliedVisualIndex_ = -1;
};

} // namespace AIO::GUI
