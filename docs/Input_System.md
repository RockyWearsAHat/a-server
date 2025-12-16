# Input System (Two-Layer Model)

This project intentionally uses **two layers** of input:

## 1) Our Application (the AIOServer window / menus)

This is the 10-foot UI “booter” experience: main menu, emulator-select, ROM-select, settings.

**Rules**

- All menu pages share one **global menu mapping**.
- Menu navigation is handled centrally through:
  - `AIO::Input::InputManager` → produces **logical buttons** (`LogicalButton::Confirm`, `Back`, `Up/Down/Left/Right`, `Home`).
  - `AIO::GUI::UIActionMapper` → turns logical buttons into `UIActionFrame` (edge + repeat).
  - `AIO::GUI::NavigationController` → applies actions to the active menu adapter.
- Menu pages should **not** implement their own controller button mapping.

**Where it happens**

- `MainWindow::UpdateDisplay()` routes menu pages through `UIActionMapper` + `NavigationController`.

## 2) Sub-Applications (apps)

Apps are the “cool things” that run inside the system:

- Emulators (GBA, Switch)
- Streaming apps (YouTube, web-based streaming)

**Rules**

- Each sub-application may define its own action set (`AppId`) and has its own remapping.
- By default, sub-app actions should match the menu’s meaning (Confirm/Back etc.), but overrides are stored **per app**.

**Persistence**

Persistence uses Qt's `QSettings`.

- macOS: typically `~/Library/Preferences/` (plist-backed)
- Windows: Registry
- Linux: typically `~/.config/` (ini-style)

Menu-layer (shared):

- Stored under `Input/MenuBindings`.
- Edited via the bindings UI when opened for `AppId::System`.

Sub-app (per app):

- Stored under `Input/ActionBindings/<App>`.
- Apps inherit menu bindings for common actions (Up/Down/Left/Right/Confirm/Back/Home) unless overridden.

## Implementation Notes

- The menu layer uses a unified routing path (no per-page controller handlers).
- Sub-app pages may rely on synthesized key events (Enter/Esc/Arrows) when appropriate, but emulator gameplay input is fed by the emulator’s own input pipeline.
