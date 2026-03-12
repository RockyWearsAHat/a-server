---
name: visual-development-loop
description: "Run the host-driven boot, navigate, capture, inspect, and judge loop for rendered-output verification in AIO Server."
user-invocable: false
---

# Visual Development Loop

Use this skill when a task needs definitive automated evidence about a rendered screen state in AIOServer.

Read `.github/instructions/visual-dev-loop.instructions.md` before running the loop.

## Purpose

- Drive the built app through host-side GUI automation when logs, tests, and deterministic headless runs are not enough to answer the visual question.
- Capture screenshots or frame dumps, inspect them with `aioserver-inspect-screenshot` so the runtime attaches the real image bytes to a vision-capable model, and make a clear pass or fail judgment about the target screen state.
- Leave the workspace and host process state clean when the run ends.

## Before You Launch

- Confirm the build is current enough for verification. If the binary is stale or missing, stop and report that verification is blocked on a build.
- Decide whether GUI or headless verification is the right fit.
- Record a short session plan in your working notes: target ROM or explicit non-ROM app flow, expected screen, navigation path, evidence to collect, and pass/fail conditions.
- For a single-screen check, prefer a boot → capture → kill flow that never focuses the app unless interaction is required.
- Prefer a single `session --plan` call when you already know the action sequence. Use individual commands only when you need to inspect state between steps.
- Do not use this workflow for idle launcher or shell-state checks unless the user explicitly asks for that exact screen.

## GUI Versus Headless

Use the host-driven GUI path when any of these are true:

- The bug depends on the real window, focus, host input, menu navigation, or a runtime UI path that is awkward to reproduce headlessly.
- You need to prove that the rendered screen reached a specific interactive state.
- You need screenshot evidence from the actual application window so the model can inspect the true rendered output.

Use a deterministic headless run first when any of these are true:

- A framebuffer dump and scripted input can answer the question faster.
- The check is about emulator output rather than host-window behavior.
- You can reproduce the state reliably with `--headless`, `--input-script`, and a dumped PPM, then inspect that image directly and use the loop script only for supporting signals.

If a headless run answers the question cleanly, report that instead of forcing GUI automation. If it does not, switch promptly to the GUI loop.

## Execution Loop

1. Build the action plan.
   Describe the ROM or explicit non-ROM target flow, timing assumptions, intended navigation, expected on-screen signals, and failure signals.

2. Launch deterministically when possible.
   Use `python3 scripts/visual_dev_loop.py boot` with `--rom` for emulator flows or `--app youtube` for direct non-ROM streaming flows, plus optional `--input-script` when applicable. In GUI mode, use `AIO_INPUT_SCRIPT_TIMEBASE=EMU` when deterministic timing is required and supported by the scenario.

For a single capture with no interaction, prefer `python3 scripts/visual_dev_loop.py snapshot` so the app opens, captures, and exits immediately. Prefer app-window capture metadata first. Fall back to full-screen capture only when window capture is unavailable or clearly blank.

3. Reach the target state.
   Prefer `python3 scripts/visual_dev_loop.py session --plan '<json>'` for known sequences that combine boot, waits, keys, clicks, screenshot, analyze, and kill. For GUI interaction, focus the app immediately before host key, click, or type input. Use `focus`, `key`, `click`, and `wait` only when you need interaction or you must recover from a navigation problem after reviewing the latest captured image.

   Do not guess click points from raw screen coordinates. If you identify a target in the captured app image, convert image-space coordinates through the current AIOServer window bounds and capture scale or use the script's image-based click support.

   Use the higher-level helpers when they remove repeated low-value work: `key-sequence` for multi-step navigation input and `click_percent` when the target is best described as a stable fraction of the captured image.

4. Capture evidence.
   Take a screenshot or retain the dumped frame for the state you intend to judge. Treat `aioserver-inspect-screenshot` as the primary decision path because it sends the actual image artifact to a vision-capable model during execution. Use `python3 scripts/visual_dev_loop.py analyze` only for supporting signals such as `--nonblack` for blank-screen detection, `--colors` for color statistics and grid layout, and `--region` for targeted crops. Use `--ocr` only when readable UI text is central to the expectation and the image alone is not enough.

   Do not use text-file reads on `.png` or `.ppm` artifacts. Those files are binary. Pass the screenshot path to `aioserver-inspect-screenshot` and rely on the generated sidecar metadata or analysis output only when the tool is unavailable.

5. Judge.
   Base the judgment first on direct inspection of the captured image against the expected screen state. Use returned JSON, non-black ratio, color statistics, region crops, log output, and status output to support the decision or resolve ambiguity.

6. Retry only when it is still a navigation problem.
   If the run is on the wrong screen or timing is off, correct the navigation and retry up to three total attempts. If the target screen is reached and the output is wrong, stop retrying and report failure with evidence.

7. Clean up.
   Always run `python3 scripts/visual_dev_loop.py kill` at the end of a GUI session, even after failures. The `snapshot` command already kills the app for you.

## Failure Recovery

- If `boot` reports that AIOServer is not built or exits immediately, stop and report the build or runtime failure rather than guessing.
- If the window cannot be focused or found, check `status`, allow for launch time, and retry once before reporting a host-interaction failure.
- If host key or click input does not move the UI, confirm the app was focused immediately before the input and that click targets came from the captured app window rather than guessed desktop coordinates.
- If the captured image shows the wrong screen, adjust navigation or timing and run another attempt.
- If analysis shows a black or near-black screen where real output is expected, treat that as a likely failure unless the test target is intentionally dark and you have stronger corroborating evidence.
- If `aioserver-inspect-screenshot` or a vision-capable model is unavailable in the runtime, report that blocker and fall back to non-black, color, region, OCR, or log evidence only as a reduced-confidence path.
- If `tesseract` or other optional host dependencies are missing, report the missing dependency clearly and continue with the remaining reliable evidence.

## Reporting Expectations

- State whether you used GUI automation or a deterministic headless path, and why.
- Report the exact commands or the `session --plan` structure at a concise level when it matters to reproducibility.
- Include artifact paths, relevant log paths, and whether the final judgment came from `aioserver-inspect-screenshot`, supporting analysis signals, or both.
- Separate navigation failures from real visual failures.
- Make a definitive pass, fail, or blocked call. Describe the captured image evidence and the automated conclusion it supports.

## Cleanup

- Kill any AIOServer instance started for the run.
- Call out leftover blockers such as missing ROMs, missing host tools, or stale builds.
- If the workflow produced screenshots or logs that matter to the result, name those paths in the report.
