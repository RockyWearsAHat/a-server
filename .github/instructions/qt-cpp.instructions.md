---
description: "Qt C++ ownership, signal-slot, and widget implementation rules for GUI code."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp"
---

# Qt C++ Rules

- Parent widgets and QObjects at construction time when Qt tree ownership is intended. Do not manually delete children that Qt already owns.
- Use modern `connect` syntax with function pointers or lambdas. For cross-thread delivery, specify `Qt::QueuedConnection` explicitly.
- If a raw `QWidget *` or `QObject *` is stored as a member, make the owning lifetime obvious from construction and parentage.
- Keep widget setup, signal wiring, and state refresh logic organized in the same style as the surrounding subsystem. Prefer helper methods over bloated constructors when the file already uses them.
- Keep `objectName`, dynamic properties, and QSS selectors synchronized in the same change.
- Prefer QSS for reusable styling and visual state. Do not hardcode one-off style strings in C++ when the subsystem already uses QSS.
- When modifying event handlers, ensure focus, selection state, and navigation helpers stay consistent across keyboard, controller, and remote-control flows.
- When a UI change also affects runtime ownership or async behavior, verify both appearance and object lifetime assumptions.

## Common Failure Prevention

- A parent deleting children is normal Qt behavior. Double-free usually means manual deletion or stale pointers.
- Signal-slot bugs across threads are usually delivery-mode bugs, not random crashes.
- If a widget state is represented in QSS, refresh properties and style polish only where needed instead of forcing broad restyles.