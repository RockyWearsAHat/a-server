---
name: visual-development-loop
description: "Run the host-driven boot, navigate, capture, inspect, and judge loop for rendered-output verification in AIO Server."
user-invocable: false
---

# Visual Development Loop

Use this skill when code inspection alone cannot answer whether the rendered output is correct.

## Emulator Runtime Verification

This skill also covers emulator accuracy verification — not just UI appearance. When checking whether emulators work with real ROMs:

- Boot app: `python3 scripts/visual_dev_loop.py boot --no-focus`
- Launch ROM via RC: `python3 scripts/visual_dev_loop.py execute launch-rom --params '{"rom":"<filename>"}'` — resolves against `test_roms/` automatically
- Poll emulator state: `poll-state --endpoint emulator` → verify `frameNumber` advances, `running` is true, no crashes
- Pause / step: `execute pause`, `execute step-frame`, `execute resume`
- Stop ROM: `execute stop-game` → returns to home screen
- Headless smoke test: `./build/bin/AIOServer --headless --rom <path> --headless-max-ms 5000 --headless-dump-ppm /tmp/frame.ppm --headless-assert-nonblack`
- Screenshot + judge: capture rendered game frame, use MCP `analyze_images` to verify it matches expected game visuals
- Available ROMs: `test_roms/` (workspace-relative) — GBA (`.gba`), PS1 (`.bin/.cue` in subdirs), Switch (`.xci`)

## Choose The Cheapest Sufficient Check First

- If the question is about widget existence, properties, layout wiring, or QSS selectors, read the source first.
- If the question is about actual on-screen appearance or behavior, use visual capture.
- If screenshot artifacts already exist, inspect them directly instead of recapturing.

## Two Mechanisms

- App control: run `python3 scripts/visual_dev_loop.py <cmd>` in the terminal for `boot`, `screenshot`, `snapshot`, `key`, `click`, `execute`, `session --plan`, `status`, `poll-state`, `poll-events`, and `kill`.
- Image judgment: call `analyze_images` through the `aioserver-vision/*` MCP tool namespace with 1–10 image paths and a freeform goal.
- Judgment criteria: follow the AAA Visual Design Audit Standard (`.github/instructions/visual-audit.instructions.md`). Use the structured output format with category scores.

Do not substitute terminal `analyze` output for MCP image judgment.

## State Polling

Before capturing a screenshot, poll app state to confirm the expected screen is visible:

```
python3 scripts/visual_dev_loop.py poll-state --endpoint state
python3 scripts/visual_dev_loop.py poll-state --endpoint navigation
python3 scripts/visual_dev_loop.py poll-state --endpoint input
python3 scripts/visual_dev_loop.py poll-state --endpoint emulator
python3 scripts/visual_dev_loop.py poll-state --endpoint audio
python3 scripts/visual_dev_loop.py poll-state --endpoint page
python3 scripts/visual_dev_loop.py poll-state --endpoint widgets
python3 scripts/visual_dev_loop.py poll-events --limit 20
```

Endpoints: `state` (full snapshot: page, inputMode, focusWidget, navigation, geometry), `navigation` (homeGrid tiles, hoveredIndex), `input` (mouse/controller mode, pressed buttons), `emulator` (type, running, paused, frameNumber), `audio` (playing, sampleRate, channels), `page` (per-page structured state: gameStore/gamesLibrary/streamingHub objects), `widgets` (visible widget tree).

`poll-events [--since <epochMs>] [--limit <N>]` streams the ring buffer (last 500 events): `navigate_requested`, `page_changed`, `emulator_started/stopped/paused/resumed`, `audio_silence_detected/resumed`, `key_injected`, `click_injected`, `launch_rom`, `game_stopped`.

Use `boot --no-focus` to launch without stealing focus from the user's current work.

## Standard Flow

1. Decide whether source inspection or visual capture is necessary.
2. If artifacts already exist, inspect them and stop.
3. If a one-shot capture is enough, use `snapshot`.
4. If interaction is required, `boot`, navigate, `screenshot`, judge immediately, and continue only if the evidence is still inconclusive.
5. After every screenshot, judge it before taking another one.
6. **Fix Feedback** — If judgment is FAIL, produce a structured fix list before handing off to implementation. For each deviation: `file → selector → property → current value → target value`. Reference the token tables in `design-system.instructions.md` for target values and the fix mapping in `visual-audit.instructions.md` for which properties to check per category. This list becomes the fix brief for the next implementation cycle.
7. Stop after a definitive pass, or after the fix-feedback list is delivered for a fail.
8. `kill` the app at the end of an interactive session.

## Fast Rules

- Do not read `.png` or `.ppm` files as text.
- Screenshot capture does not need focus.
- Only focus before input when focus is actually required.
- `snapshot` is the fast path for non-interactive checks.
- `session --plan` is the fast path for known interactive paths.
- Report pass, fail, or blocked with evidence and the smallest actionable follow-up.
