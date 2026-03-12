When patching multiple locations in files, always plan well then start the patching from the bottom of the file up. Thus, content shifts don't happen causing subsequent patches to fail. This is annoying and a waste of turns and tokens, often causing confusion because of an easily avoidable badly explained behavior [it's literally not coded in copilot {atleast not consistently that I've noticed}, I am defining it here for clarity if it has already been mentioned earlier, this is simple reenforcement].

Please ensure that you understand what system the user is on. This should be found and placed in the #file:[./github/SYSTEM.md](./SYSTEM.md), if this file is not created please create it. In the case it is missing, assume system information must still be found for this local machine AND MAKE THIS AN IMMEDIATE PRIORITY. If the file can be found or has been filled out/registered then use it. Ensure this system.md outlines what platform the user is developing on, default terminal configuration and environment the user is running on. Etc. If anything necessary or helpful is missing from the created version, add it. If the file must still be created, make it. Anything that will be helpful in clarifying commands and development ON THE LOCAL MACHINE should live there, it should not be instructions but rather a reference for what is valid for the envioronment.

This repository is a native C++ emulator application built with Qt, CMake, and a top-level `Makefile` wrapper. Most product work lands in `src/`, `include/`, `assets/qss/`, `tests/`, and `server/`.

Before broad changes, read the relevant files under `.github/instructions/` instead of guessing conventions.

Build and validation:

- Use `make build` from the repository root or the workspace Build task.
- Run tests from `build/generated/cmake` with `ctest --output-on-failure`, or use the workspace test tasks.
- If you edit `assets/qss/*.qss`, treat a successful build as required validation because `QssValidator` runs during the build.

Runtime and verification:

- Check `debug.log` after Qt runs or headless emulator flows unless the command overrides the log path.
- Prefer tests, debugger inspection, logs, and deterministic headless runs when they answer the question.
- Once the build is current, route rendered-output bugs, GUI regressions, and runtime screen-state validation promptly to `Visual Development Tester` or `Visual Development Loop`.
- Outside `Visual Development Loop`, never claim direct visual verification from screenshots, PPMs, or video captures; report automated checks and ask the user to confirm appearance.

Qt and styling workflow:

- Keep widget code, object names, dynamic properties, and matching QSS selectors synchronized.
- Keep YouTube-specific styling in `assets/qss/youtube.qss` and shared TV-shell styling in `assets/qss/tv.qss`.

Agent routing:

- Use `Expert C++ Software Engineer` for C++, Qt, emulator, architecture, and refactor work.
- Use `Visual Development Tester` for automated evidence capture, `debug.log` inspection, media artifacts, and user-facing visual or audio verification workflows.
- Use `Visual Development Loop` for host-driven boot → navigate → capture → multimodal inspect → support with analysis → judge cycles that make definitive automated screen-state checks.

Execution expectations:

- Prefer existing workspace tasks and repository commands over inventing new ones.
- Keep repository-specific customization factual, concise, and limited to this codebase.
