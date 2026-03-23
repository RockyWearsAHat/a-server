---
description: "Local development platform and execution environment."
applyTo: "src/**,include/**,tests/**,scripts/**,Makefile,cmake/**"
---

# Local System

- Platform: macOS (arm64)
- Shell: zsh
- Build entry point: `make build`
- Generated CMake build directory: `build/generated/cmake`
- Test entry point: `cd build/generated/cmake && ctest --output-on-failure`
- Qt and visual-development workflows run on the local macOS host — `osascript` and `screencapture` are valid.
- Commands and paths should assume macOS ARM64.
