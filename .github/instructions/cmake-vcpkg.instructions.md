---
description: "C++ project configuration and package management"
applyTo: "Makefile, cmake/**, **/*.cmake, **/CMakeLists.txt"
---

This repository builds through the top-level `Makefile`, which delegates to `cmake/Makefile` and then configures `build/generated/cmake` with CMake and Ninja.

- Keep build and configuration guidance aligned with that checked-in Makefile to CMake to Ninja flow.
- Do not assume `CMakePresets.json` or vcpkg manifest mode for this repository unless those files are added.
- Keep CMake guidance compatible with MSVC, Clang, and GCC.
- Prefer recommendations that fit the existing CMake files and repository build scripts over generic package-manager instructions.
- Edit the nearest owning `CMakeLists.txt` or `.cmake` file instead of introducing new targets or helper files unless the request requires it.
- Match existing target structure and naming. Do not create parallel build paths for the same subsystem.
- Prefer target-scoped commands such as `target_sources`, `target_include_directories`, `target_compile_definitions`, and `target_link_libraries` over global settings.
- Keep dependency visibility minimal: use `PRIVATE` unless headers or consumers actually require `PUBLIC` or `INTERFACE` exposure.
- When adding source files, update the owning target and any related tests in the same pass.
- Avoid broad compiler-flag changes unless the repository already applies that pattern globally.
- After CMake changes, verify with `make build` rather than ad hoc generator commands.

## Sanitizer Gate Guidance

- Verification workflows may require a sanitizer-clean run (ASan/UBSan) for emulator behavior changes.
- If sanitizer flags are introduced, keep them opt-in (for example, through a dedicated cache variable) rather than always-on in release-style builds.
- Document sanitizer enable/disable commands in the owning CMake file comments so test workflows can invoke Layer 1 consistently.
