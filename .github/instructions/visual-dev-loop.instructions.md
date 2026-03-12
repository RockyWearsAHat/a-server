---
description: "Durable facts for the host-driven visual development loop workflow."
applyTo: "scripts/visual_dev_loop.py,.github/agents/visual-dev-loop.agent.md"
---

# Visual Development Loop Instructions

`scripts/visual_dev_loop.py` is the repository's host-driven GUI interaction and capture helper for rendered-output verification on macOS.

`tools/aioserver-vision-tool` is the workspace-local VS Code extension that contributes `aioserver-inspect-screenshot`, the language-model tool that sends real screenshot image bytes to a vision-capable Copilot model during execution.

The `Visual Development Loop` agent should inspect captured screenshots or frame dumps through `aioserver-inspect-screenshot`. Script analysis is supporting evidence, not the primary judgment path.

## Host Dependencies

- Built-in macOS tools: `screencapture` for capture and `osascript` for window focus.
- Required host tools: `cliclick` for keyboard and mouse automation and Python with `Pillow` available to the script.
- Optional host tool: `tesseract` for fallback OCR when visible text is important and direct image inspection needs support.

## JSON Contract

- Every command prints structured JSON to stdout.
- The script exposes host-interaction commands for boot, focus, key, click, type, wait, screenshot, snapshot, status, kill, and analyze.
- The `session` command accepts a JSON array of action objects and returns per-step results plus `last_screenshot` when a capture succeeds.
- Captured screenshot paths are the primary artifacts for multimodal review through `aioserver-inspect-screenshot`; `analyze` output is supplemental.
- Each screenshot also gets a JSON sidecar with capture mode, window bounds, capture scale, and display scale so later clicks can map image coordinates accurately.

## Deterministic Playback Notes

- The script can boot AIOServer with `--input-script` for repeatable startup and navigation.
- The script can boot directly into supported non-ROM streaming flows with `--app` such as `youtube`, which avoids brittle launcher navigation when the target screen is known.
- Input scripts run with emulated time by default in headless mode and wall-clock time in GUI mode.
- Set `AIO_INPUT_SCRIPT_TIMEBASE=EMU` when deterministic timing is required in GUI mode.

## Scope

- This workflow exists for host-driven GUI interaction and direct screenshot or frame inspection when automated screen-state judgments are required.
- The normal targets are ROM-driven gameplay screens or explicit non-ROM application flows, not idle launcher or shell states.
- For single-screen checks, prefer launching in the background, capturing once, and terminating immediately rather than focusing the window and keeping the app open.
- Prefer app-window capture metadata first. Full-screen capture is a fallback, not the default success path.
- For GUI interaction, host key, click, and type input must target the focused AIOServer window. Raw input sent to an unfocused app should be treated as unreliable.
- Screenshot pixels are not desktop coordinates. Convert image-space click targets through the current AIOServer window bounds and capture scale before clicking.
- If the capture was full-screen, convert image pixels through the recorded display scale instead of treating them as window-relative.
- Do not read screenshot binaries as text. The supported path for actual image understanding is the workspace-local `aioserver-inspect-screenshot` tool.
- Use the paired repo-local skill for the procedural boot, navigate, capture, analyze, judge loop and recovery steps.
