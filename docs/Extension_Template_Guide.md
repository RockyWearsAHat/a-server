# Extending Navigation to Remaining Menus

This guide shows how to add controller/keyboard/mouse support to the remaining app menus using the established ButtonListAdapter pattern.

## Pattern Template

For any new menu, follow this 3-step pattern:

### Step 1: Create Adapter Header

```cpp
// include/gui/YourMenuAdapter.h
#pragma once

#include "gui/ButtonListAdapter.h"
#include <QWidget>

class QPushButton;

namespace AIO::GUI {

class MainWindow;

class YourMenuAdapter final : public ButtonListAdapter {
public:
    YourMenuAdapter(QWidget* page,
                    const std::vector<QPushButton*>& buttons,
                    MainWindow* owner);

    bool back() override;

private:
    MainWindow* owner_;
};

} // namespace AIO::GUI
```

### Step 2: Create Adapter Implementation

```cpp
// src/gui/YourMenuAdapter.cpp
#include "gui/YourMenuAdapter.h"
#include "gui/MainWindow.h"

namespace AIO::GUI {

YourMenuAdapter::YourMenuAdapter(QWidget* page,
                                 const std::vector<QPushButton*>& buttons,
                                 MainWindow* owner)
    : ButtonListAdapter(page, buttons), owner_(owner) {}

bool YourMenuAdapter::back() {
    if (owner_) {
        owner_->goToPreviousMenu();  // Your navigation call
        return true;
    }
    return false;
}

} // namespace AIO::GUI
```

### Step 3: Integration in MainWindow

#### In setupYourMenu():

```cpp
void MainWindow::setupYourMenu() {
    yourMenuPage = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(yourMenuPage);

    // Create buttons
    QPushButton *btn1 = new QPushButton("Option 1", yourMenuPage);
    QPushButton *btn2 = new QPushButton("Option 2", yourMenuPage);
    QPushButton *backBtn = new QPushButton("Back", yourMenuPage);

    // Wire up clicks
    connect(btn1, &QPushButton::clicked, this, [this]() { /* action */ });
    connect(btn2, &QPushButton::clicked, this, [this]() { /* action */ });
    connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);

    // Add to layout
    layout->addWidget(btn1);
    layout->addWidget(btn2);
    layout->addWidget(backBtn);

    // Create adapter with ALL interactive buttons
    yourMenuAdapter = std::make_unique<YourMenuAdapter>(
        yourMenuPage,
        std::vector<QPushButton*>{btn1, btn2, backBtn},
        this
    );
}
```

#### In MainWindow::onPageChanged():

```cpp
if (current == yourMenuPage) {
    nav.setAdapter(yourMenuAdapter.get());
    if (yourMenuAdapter) {
        yourMenuAdapter->applyHovered();
    }
    return;
}
```

---

## Remaining Menus to Implement

### 1. Streaming Hub Menu

**File**: `src/gui/StreamingHubWidget.cpp`
**Buttons**: Netflix, Disney+, Hulu, YouTube, Back
**Status**: Likely already has buttons, just needs adapter wrapper

**Implementation Steps**:

1. Create `StreamingHubAdapter` class
2. Extract buttons from `StreamingHubWidget`
3. Pass to ButtonListAdapter constructor
4. Register in `onPageChanged()`

### 2. YouTube Browse Page

**File**: `src/gui/YouTubeBrowsePage.cpp`
**Buttons**: Depends on UI structure (may be grid-based)
**Status**: May need `GridLayoutAdapter` variant if 2D grid layout

**If Button-Based**:

- Follow standard pattern
- Create `YouTubeBrowseAdapter`

**If Grid-Based**:

- Future enhancement: Create `GridLayoutAdapter` that supports Up/Down/Left/Right
- Would extend ButtonListAdapter with 2D navigation logic

### 3. YouTube Player Page

**File**: `src/gui/YouTubePlayerPage.cpp`
**Buttons**: Play/Pause, Back, etc.
**Status**: Likely standard button controls

**Implementation Steps**:

1. Create `YouTubePlayerAdapter`
2. Identify control buttons
3. Wire up with adapter

### 4. Streaming Web View Page

**File**: `src/gui/StreamingWebViewPage.cpp`
**Challenge**: Qt WebEngine may need special handling
**Status**: May require custom navigation

**Implementation Options**:

- Option A: Simple button adapter for back/home buttons
- Option B: Custom adapter that bridges to WebEngine JavaScript
- Recommend starting with Option A (buttons only)

---

## Menus Currently Using ButtonListAdapter

| Menu            | Adapter               | Status      | Buttons                    |
| --------------- | --------------------- | ----------- | -------------------------- |
| Main Menu       | MainMenuAdapter       | ✅ Complete | Play, Settings, Quit, Back |
| Emulator Select | EmulatorSelectAdapter | ✅ Complete | GBA, Switch, Back          |
| Game Select     | GameSelectAdapter     | ✅ Complete | Back (+ QListWidget)       |
| Settings        | SettingsMenuAdapter   | ✅ Complete | Browse, Back               |
| Streaming Hub   | TBD                   | 🔄 Ready    | Netflix, Disney+, etc.     |
| YouTube Browse  | TBD                   | 🔄 Ready    | Depends on layout          |
| YouTube Player  | TBD                   | 🔄 Ready    | Controls + Back            |
| Web View        | TBD                   | 🔄 Ready    | Back/Home button           |

---

## Priority Implementation Order

1. **High Priority**: Streaming Hub (most user-facing)
2. **High Priority**: YouTube Browse (primary streaming content)
3. **Medium Priority**: YouTube Player (secondary controls)
4. **Low Priority**: Web View (fallback for other services)

---

## Build Integration

After creating each new adapter:

1. **Update cmake/core.cmake**:

```cmake
# Add to AIOServer executable sources:
${PROJECT_ROOT}/src/gui/YourMenuAdapter.cpp
```

2. **Update MainWindow.h**:

```cpp
namespace AIO::GUI {
    class YourMenuAdapter;
}

// In private section:
std::unique_ptr<AIO::GUI::YourMenuAdapter> yourMenuAdapter;
```

3. **Update MainWindow.cpp**:

```cpp
// In includes:
#include "gui/YourMenuAdapter.h"

// In setupYourMenu():
yourMenuAdapter = std::make_unique<YourMenuAdapter>(...);

// In onPageChanged():
if (current == yourMenuPage) {
    nav.setAdapter(yourMenuAdapter.get());
    if (yourMenuAdapter) {
        yourMenuAdapter->applyHovered();
    }
    return;
}
```

---

## Testing Each Menu

For each new menu adapter, test:

1. **Keyboard Navigation**:

   - Arrow keys move between buttons
   - Enter/Space activates button
   - Backspace/ESC triggers back button

2. **Mouse Navigation**:

   - Hover over buttons shows highlight
   - Click activates button
   - Cursor should be visible in mouse mode

3. **Controller Navigation**:

   - D-Pad/left stick moves between buttons
   - A button activates button
   - B button triggers back

4. **Mode Switching**:

   - Switch from controller to mouse: hover should work, cursor visible
   - Switch from mouse to controller: resume from last controller position
   - Check that first button gets consistent styling

5. **Resume Positions**:
   - Navigate with controller, remember position
   - Switch to mouse mode
   - Switch back to controller: should resume from remembered position
   - Vice versa for mouse → controller transition

---

## Common Pitfalls to Avoid

❌ **Don't**: Forget to pass owner pointer for back() navigation  
✅ **Do**: Always pass `this` from MainWindow constructor

❌ **Don't**: Include non-clickable widgets in button vector  
✅ **Do**: Only pass actual QPushButton\* pointers

❌ **Don't**: Forget to call `applyHovered()` in onPageChanged()  
✅ **Do**: Always call it after setAdapter()

❌ **Don't**: Use boolean properties in QSS selectors  
✅ **Do**: Use string properties like `aio_hovered="true"` (not `aio_hovered=true`)

❌ **Don't**: Create multiple adapters for same page  
✅ **Do**: One adapter per page, passed to NavigationController

---

## Performance Notes

- ButtonListAdapter is lightweight (~1.5KB per instance)
- Mouse polling only active in Mouse mode (16ms interval)
- Controller input uses edge detection (triggers on transition)
- No performance overhead from adding more menus
- All adapters can coexist simultaneously without issues

---

## Future Enhancements

### GridLayoutAdapter (For 2D Layouts)

When YouTube Browse needs 2D navigation:

```cpp
class GridLayoutAdapter : public ButtonListAdapter {
public:
    GridLayoutAdapter(QWidget* page, int cols, int rows, ...);
    void onUp() override;    // Move up in grid
    void onDown() override;  // Move down in grid
    void onLeft() override;  // Move left in grid
    void onRight() override; // Move right in grid
};
```

### ScrollableListAdapter (For Large Lists)

When game list grows beyond visible area:

```cpp
class ScrollableListAdapter : public NavigationAdapter {
public:
    ScrollableListAdapter(QWidget* page, QListWidget* list);
    void onDown() override;    // Scroll down, wrapping if needed
    void onUp() override;      // Scroll up, wrapping if needed
};
```

### CustomKeyboardAdapter (For Special Keys)

For menus needing more than directional/A/B:

```cpp
class CustomKeyboardAdapter : public ButtonListAdapter {
public:
    void onKeyDown(int qtKeyCode) override;  // Custom key handling
};
```

---

## Support

For issues implementing new menus:

1. Check the `Input_Navigation_System.md` guide
2. Compare with existing adapter in `MainMenuAdapter` or `EmulatorSelectAdapter`
3. Ensure all includes are properly updated in MainWindow.h/cpp
4. Verify button vector includes all interactive buttons
5. Check cmake/core.cmake has the .cpp file listed
