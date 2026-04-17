AIO Server is a native TV operating shell built with Qt 6, C++, and CMake, with a small Node.js backend for supporting services.

Build: `make build`
Targeted tests: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`

## Checkpointing
Use parameter `message: "your commit message"` to commit without an AI call. The context: parameter calls a subagent, so save the subagent call when possible and please prefer usage of the message parameter instead and just write your own commit message based upon the diff and changes made.

## A Note on Documentation
Documentation is only as good as it is made to be, it's only as accurate as it's kept. Please, as you edit and make changes, consider updating knowledge documents and documentation. Along with the actual proper coding principles defined in the CS3500 doc you already follow, documentation really helps for long-running continued knowledge. Keep things consise but also keep them updated with ACTUALLY PROPER INFORMATION REVISING, DELETING, AND CREATING AS YOU GO SO ANY AGENT CAN GO BACK AND REFERENCE WHAT IS NEEDED **ACCURATELY!!!**

DO NOT touch plans unless the user has asked you to, but every other bit of knowledge and reference is your responsibility and meant to help you and other agents as you work to more accurately find what you need, when you need it, and ensure a clear picture is already known about the state instead of a new agent having to retest or context potentially being compacted out of your history. We have history reviews (to hopefully avoid the compaction issue, but that doesn't change a new agent has to look for a specific function, or potentially could get a bad idea of how a piece works because of non-thorough reading due to size constraints.) Most importantly, before, during, and after code, it is your job to **ALWAYS** review and understand documentation [pre-code], plus update it as needed [post-code] and the codebase evolves with work [REFERENCE THROUGHOUT WORK ALWAYS TO ENSURE ACCURATE PICTURE OF FUNCTIONS, SETUPS, MERMAID DIAGRAMS OF PROJECT, ETC ETC, IF CODE CONFLICTS WITH DOCUMENTATION, UPDATE DOCUMENTATION, BUT IF A BUG IS FOUND (DOCUMENTATION SPECIFIES SOMETHING SHOULD BE THIS WAY **AND WHY WITH A SOURCE** [TRUE REASON, ACTUAL BUG!!]) FIX IT IMMEDIATLEY, OUR CODE IS WRONG AND THIS IS THE REASON WE KEEP THE DOCUMENTATION, IT COULD HAVE DIVERGED IN ANOTHER EDIT OR IT COULD HAVE BEEN MODIFIED BY ANOTHER PERSON, EITHER WAY IT'S BROKEN SO IN THIS CASE WE NEED TO FIGURE OUT WHAT THE RIGHT SOLUTION IS **VERIFIABLY** AND USE THAT TO REFINE OUR CODE TO BE **PERFECT**].

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
