---
description: "Repo-specific facts for visual and audio verification workflows."
applyTo: "src/gui/**/*.cpp,include/gui/**/*.h,include/gui/**/*.hpp,assets/qss/**/*.qss,src/emulator/**/*.cpp,include/emulator/**/*.h,include/emulator/**/*.hpp,tests/**/*Tests.cpp"
---

# Visual Development Testing Instructions

Agents cannot directly verify screenshots, PPM images, videos, or audio quality. Report only automated checks and ask the user to confirm rendered or audible output.

- Prefer the VS Code debugger, logs, automated tests, and deterministic headless runs before visual capture when those answer the question.
- `debug.log` is the default AIOServer log sink unless a command overrides the path.
- `--record-av` is the built-in path for synchronized audio and video capture.
- `--headless-dump-ppm` dumps framebuffer output for automated inspection or user review.
- `--dump-audio` captures emulator audio output to a WAV file.
- Prefer deterministic headless runs and input scripts when reproducing emulator behavior.
- Capture and report artifact paths, relevant metadata, and log locations for every verification run.
- Use `debug.log` or the overridden log path as part of the evidence set for runtime verification.
- Treat ROM filenames as user-supplied inputs rather than assuming a specific checked-in test ROM is available.
- Never claim direct visual verification from screenshots, PPMs, or video captures.
- When the task needs a definitive automated judgment about a specific rendered screen state, hand off to the `Visual Development Loop` workflow instead of keeping that judgment in general testing guidance.
- `Visual Development Tester` remains the artifact and log collection path; multimodal screenshot judgment belongs to `Visual Development Loop`.
