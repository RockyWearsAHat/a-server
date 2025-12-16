# Button-Based Menu Navigation System Guide

## Overview

The new `ButtonListAdapter` class provides a reusable, componentized solution for implementing universal input navigation (keyboard, mouse, and controller) in any menu with QPushButton items.

## Architecture

### Core Components

1. **ButtonListAdapter** - Generic button-list adapter

   - Implements `NavigationAdapter` interface
   - Handles all navigation logic: state tracking, hover styling, resume positions
   - Can be reused across all menus with button lists

2. **NavigationController** - Input routing engine

   - Processes keyboard/controller inputs
   - Routes directional inputs to the adapter
   - Manages view transitions

3. **UIActionMapper** - Input transformation
   - Converts raw SDL joystick + Qt keyboard events into logical UI actions
   - Implements repeat logic with configurable timing

### State Tracking

ButtonListAdapter tracks:

- `hovered_`: Current controller selection index
- `mouseHover_`: Current mouse hover index (visual overlay)
- `lastMouseHover_`: Previous mouse position (for resuming)
- `lastControllerIndex_`: Previous controller position (for resuming)
- Timestamps for each interaction to determine which is most recent

## How to Add Controller Support to a New Menu

### Step 1: Create an Adapter

Option A: Use ButtonListAdapter directly (preferred for simple button menus):

```cpp
// In your menu setup
auto adapter = std::make_unique<AIO::GUI::ButtonListAdapter>(
    myPage,
    std::vector<QPushButton*>{button1, button2, button3}
);
```

Option B: Extend ButtonListAdapter for custom behavior:

```cpp
class MyMenuAdapter : public ButtonListAdapter {
public:
    MyMenuAdapter(QWidget* page, const std::vector<QPushButton*>& buttons)
        : ButtonListAdapter(page, buttons) {}

    // Override if your menu needs custom back button handling
    bool back() override {
        // Navigate back to parent menu
        return true;
    }
};
```

### Step 2: Register with Navigation Controller

```cpp
// When page changes
if (currentWidget == myPage) {
    nav.setAdapter(myMenuAdapter.get());
    myMenuAdapter->applyHovered();
}
```

### Step 3: Wire Up Hover State Display

The adapter automatically applies the `aio_hovered` property to buttons. Ensure your QSS includes:

```qss
QPushButton[aio_hovered="true"] {
  border: 2px solid rgba(170, 179, 197, 0.75);
  background-color: rgba(255, 255, 255, 0.06);
}
```

## Integration Pattern

The MainWindow navigation timer (16ms interval) handles:

1. **Controller Input Detection**:

   - Detects D-Pad/joystick input
   - Switches to Controller mode
   - Resumes from last hovered/selected position
   - Updates NavigationController state

2. **Mouse Tracking** (when in Mouse mode):

   - Polls cursor position continuously
   - Updates hover overlay when over buttons
   - Switches to Controller mode on input

3. **Input Processing**:
   - Routes actions to onUIAction()
   - NavigationController processes and updates adapter
   - Adapter updates visual styling

## Key Features

### 1. Resume Position Tracking

When switching between mouse and controller, the system remembers:

- Last mouse hover position
- Last controller selection
- Uses timestamp comparison to determine which is more recent

### 2. Consistent Styling

- Single `aio_hovered="true"` property drives all styling
- Works for both mouse and controller
- First button gets consistent styling on initial input

### 3. Repeat Timing

- Initial delay: 400ms (before first repeat)
- Repeat interval: 150ms
- Configurable in UIActionMapper.cpp

### 4. Universal Input Support

- Keyboard: Arrow keys for navigation, Enter to activate
- Mouse: Click to activate, hover for selection
- Controller: D-Pad/joystick, A button to activate

## Menus Requiring Implementation

Current menus that need this system:

- [ ] Emulator Select
- [ ] Game Select
- [ ] Settings
- [ ] Streaming Hub
- [ ] YouTube Browse
- [ ] YouTube Player
- [ ] Streaming Web View

## Implementation Checklist for Each Menu

1. ✅ Create ButtonListAdapter (or subclass for custom behavior)
2. ✅ Store as unique_ptr in MainWindow
3. ✅ Wire up in setupXxxPage() method
4. ✅ Register in onPageChanged()
5. ✅ Ensure QSS includes aio_hovered styling
6. ✅ Test navigation with keyboard, mouse, and controller

## Example: Adding Controller to Emulator Select Menu

```cpp
// In MainWindow setup
void setupEmulatorSelectPage() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    std::vector<QPushButton*> emuButtons;

    for (const auto& emu : emulators) {
        auto* btn = new QPushButton(emu.name);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, emu]() {
            selectEmulator(emu);
        });
        emuButtons.push_back(btn);
        layout->addWidget(btn);
    }

    // Create adapter
    emulatorSelectAdapter = std::make_unique<AIO::GUI::ButtonListAdapter>(
        page, emuButtons
    );

    stackedWidget->addWidget(page);
    emulatorSelectPage = page;
}

// In onPageChanged()
if (current == emulatorSelectPage) {
    nav.setAdapter(emulatorSelectAdapter.get());
    emulatorSelectAdapter->applyHovered();
}
```

## Performance Notes

- Polling loop runs every 16ms (matches typical display refresh)
- Mouse polling only active in Mouse mode
- Stylesheet re-evaluation minimized with property change detection
- QApplication::processEvents() ensures immediate visual updates

## Testing Recommendations

1. Test each input method (keyboard, mouse, controller) separately
2. Test transitions between input methods (mouse → controller → mouse)
3. Verify resume positions are correct
4. Test repeat timing doesn't cause skipped items
5. Verify styling applies consistently to first button

## Future Enhancements

Possible extensions to ButtonListAdapter:

- Grid-based navigation (left/right/up/down on 2D layouts)
- Scrollable lists with viewport clipping
- Nested menu hierarchies
- Custom activation animations
- Accessibility features (screen reader support)
