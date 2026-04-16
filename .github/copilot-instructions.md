AIO Server is a native TV operating shell built with Qt 6, C++, and CMake, with a small Node.js backend for supporting services.

Build: `make build`
Targeted tests: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`

## Checkpointing
Use message: "your commit message" to commit without an AI call. The context: parameter calls a subagent, so save the subagent call when possible and please prefer usage of the message parameter if you can write your own message instead.

## Product Philosophy

- No webview filler for primary app experiences. Native UI is the default requirement.
- The Game Store is a white-label native platform surface, not a Steam-branded wrapper.
- The Home Screen is the crown-jewel surface and must feel fast, symmetric, and deeply integrated.
- Non-emulation UI must clear the AAA visual audit bar.

## Repository Map

- `src/gui/`, `include/gui/`: Qt widgets, pages, shell navigation, QSS-backed UI.
- `src/emulator/`, `include/emulator/`: GBA and PS1 emulator subsystems.
- `tests/`: targeted native tests and QSS validation.
- `assets/qss/`: shared TV-shell and app-specific styling.
- `scripts/`: local development and visual verification tooling.
- `.github/knowledge/`: durable architecture and workflow notes.

## Always-On Rules

- Fix root cause. Do not mask failures or broaden tolerances.
- Keep changes scoped and consistent with surrounding code.
- Read code, not comments, to determine what is actually implemented.
- When behavior changes, run the smallest sufficient verification for the affected subsystem.
- Keep widget object names, dynamic properties, and QSS selectors synchronized.
- For emulator work, use official technical documentation and repo knowledge notes, not other emulators or hardware folklore.
- Check `.github/knowledge/` before external research.

## Native Code Guidance

- Language- and subsystem-specific rules live in scoped instruction files under `.github/instructions/`.
- For C++ edits, load the matching scoped instructions instead of inventing local conventions.
- If a rule is already enforced by formatters, validators, or existing file patterns, do not restate it in a larger prompt.

## Technical References

- Source map: `.github/knowledge/source-map.md`
- GUI architecture: `.github/knowledge/gui-architecture.md`
- PS1 architecture: `.github/knowledge/ps1-emulator-architecture.md`
- GBA architecture: `.github/knowledge/gba-emulator-architecture.md`
- Design system: `.github/knowledge/design-system.md`
- Copilot layout: `.github/knowledge/copilot-customization-layout.md`
- Test scoping: `.github/instructions/test-scoping.instructions.md`
- Build rules: `.github/instructions/cmake-vcpkg.instructions.md`

## Documentation Hygiene

- Keep this file short, factual, and repository-wide.
- Put C++/Qt/CMake specifics in file-based instructions.
- Put repeatable multi-step methodology in skills, not in this file.

## Reality Checks

- Some class names and older docs describe target state, not verified implementation state. Confirm current behavior in code before treating a feature as complete.
- Mutable feature status belongs in human-facing docs and `.github/knowledge/`, not in this always-on runtime file.
- For current subsystem facts, start with `.github/knowledge/source-map.md`, `.github/knowledge/gui-architecture.md`, and the relevant subsystem note.
