#pragma once

#include "gui/NavigationAdapter.h"

#include <QPointer>
#include <QPushButton>
#include <vector>

namespace AIO::GUI {

class MainWindow;

class MainMenuAdapter final : public NavigationAdapter {
public:
    MainMenuAdapter(MainWindow* owner, QWidget* page, const std::vector<QPushButton*>& buttons);

    QWidget* pageWidget() const override { return page_; }
    int itemCount() const override;
    void setHoveredIndex(int index) override;
    void activateIndex(int index) override;
    bool back() override;

    // Returns index in adapter order for a given button, or -1.
    int indexOfButton(const QPushButton* button) const;

    // Get direct access to button list for setup.
    const std::vector<QPointer<QPushButton>>& getButtons() const { return buttons_; }
    
    // Set which index is visually hovered (from mouse) - overrides controller selection display
    void setMouseHoverIndex(int index) override;
    
    // Clear mouse hover and return to showing controller selection
    void clearMouseHover() override;
    
    // Save current controller index before switching to mouse mode
    void saveControllerIndexBeforeMouse();
    
    // Called when user actually navigates with controller (not just resuming)
    void onControllerNavigation(int newIndex);
    
    // Apply the current hover state to buttons (public for initialization)
    void applyHovered();
    
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
    QPointer<MainWindow> owner_;
    QPointer<QWidget> page_;
    std::vector<QPointer<QPushButton>> buttons_;
    int hovered_ = -1;  // Controller selection
    int mouseHover_ = -1;  // Mouse hover (when set, overrides visual display)
    int lastMouseHover_ = -1;  // Last position mouse was hovering (for resuming controller)
    int lastControllerIndex_ = -1;  // Last controller index before switching to mouse
    int mouseHoverTimestamp_ = 0;  // When lastMouseHover_ was set
    int controllerIndexTimestamp_ = 0;  // When lastControllerIndex_ was set
};

} // namespace AIO::GUI

