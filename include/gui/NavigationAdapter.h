#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>

namespace AIO::GUI {

// Logical UI actions independent of input device.
enum class UIAction {
    None,
    Up,
    Down,
    Left,
    Right,
    Select,
    Back,
    Home,
};

// Input source tracking is used to hide/show cursor and resolve hover.
enum class UIInputSource {
    Unknown,
    Mouse,
    Keyboard,
    Controller,
};

struct UIActionFrame {
    UIAction primary = UIAction::None;
    UIInputSource source = UIInputSource::Unknown;
};

// Adapter for pages that expose a simple "selectable items" model.
class NavigationAdapter {
public:
    virtual ~NavigationAdapter() = default;

    // Root widget for this page (used for properties / repaint).
    virtual QWidget* pageWidget() const = 0;

    // Number of selectable items.
    virtual int itemCount() const = 0;

    // Apply hovered selection to visuals (single unified outline).
    virtual void setHoveredIndex(int index) = 0;

    // Activate/select current hovered item.
    virtual void activateIndex(int index) = 0;

    // Optional back action; return true if handled.
    virtual bool back() = 0;
    
    // Set mouse hover override (visually shows selection, doesn't affect controller selection)
    virtual void setMouseHoverIndex(int index) {}
    
    // Clear mouse hover and return to controller selection display
    virtual void clearMouseHover() {}
};

} // namespace AIO::GUI

