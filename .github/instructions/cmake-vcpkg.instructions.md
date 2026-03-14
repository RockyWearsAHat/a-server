---
description: "C++ project configuration and package management"
applyTo: "Makefile, cmake/**, **/*.cmake, **/CMakeLists.txt"
---

This repository builds through the top-level `Makefile`, which delegates to `cmake/Makefile` and then configures `build/generated/cmake` with CMake and Ninja.

- Keep build and configuration guidance aligned with that checked-in Makefile to CMake to Ninja flow.
- Do not assume `CMakePresets.json` or vcpkg manifest mode for this repository unless those files are added.
- Keep CMake guidance compatible with MSVC, Clang, and GCC.
- Prefer recommendations that fit the existing CMake files and repository build scripts over generic package-manager instructions.
