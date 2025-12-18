# Code Conventions (AIO)

This repo aims for **dumb-simple, self-explanatory C++**.

## 1) File Organization

- Public headers live in `include/<area>/...`
- Implementations live in `src/<area>/...`
- When a `.cpp` grows large or mixes concerns, split it into focused units:
  - Example pattern: `src/gui/mainwindow/MainWindow_*.cpp`
  - Prefer `src/<area>/<module>/<Module>_*.cpp` for multi-file modules.
- Keep names action-oriented and obvious:
  - `*_Routing.cpp`, `*_Http.cpp`, `*_Input.cpp`, `*_Audio.cpp`, etc.

## 2) "C++ Javadoc" Documentation (Doxygen)

Use **Doxygen-style** comments for public classes/functions and anything non-obvious.

### Class docs

```cpp
/**
 * @brief One-line description.
 *
 * Longer explanation, invariants, and any safety/accuracy notes.
 */
class Thing { ... };
```

### Function docs

```cpp
/**
 * @brief What it does.
 * @param foo What this parameter means.
 * @return What the caller can expect.
 */
int DoThing(int foo);
```

Guidelines:

- Document _intent_ and _invariants_, not the obvious mechanics.
- Prefer describing constraints (threading, ownership, timing, emulation accuracy rules).

## 3) Simplicity Rules

- Prefer early returns over deep nesting.
- Keep functions small and single-purpose.
- Avoid surprising side effects.
- Don’t add abstractions unless they remove real duplication or complexity.

## 4) Comments

Add comments when the code is correct but the reason isn’t obvious.
Good examples:

- Hardware quirks / GBATEK notes
- Security/sandboxing constraints
- Timing/cycle-accuracy constraints
- Platform-specific pitfalls (QtWebEngine/macOS, etc.)
