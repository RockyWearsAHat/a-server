---
name: visual-development-loop
description: "Run the host-driven boot, navigate, capture, inspect, and judge loop for rendered-output verification in AIO Server."
user-invocable: false
---

# Visual Development Loop

Use this skill when code inspection alone cannot answer whether the rendered output is correct.

## Choose The Cheapest Sufficient Check First

- If the question is about widget existence, properties, layout wiring, or QSS selectors, read the source first.
- If the question is about actual on-screen appearance or behavior, use visual capture.
- If screenshot artifacts already exist, inspect them directly instead of recapturing.

## Two Mechanisms

- App control: run `python3 scripts/visual_dev_loop.py <cmd>` in the terminal for `boot`, `screenshot`, `snapshot`, `key`, `click`, `session --plan`, `status`, `poll-state`, and `kill`.
- Image judgment: call `analyze_images` (MCP tool `mcp_aioserver-vis_analyze_images`) with 1–10 image paths and a freeform goal.

Do not substitute terminal `analyze` output for MCP image judgment.

## State Polling

Before capturing a screenshot, poll app state to confirm the expected screen is visible:

```
python3 scripts/visual_dev_loop.py poll-state --endpoint state
python3 scripts/visual_dev_loop.py poll-state --endpoint navigation
python3 scripts/visual_dev_loop.py poll-state --endpoint input
python3 scripts/visual_dev_loop.py poll-state --endpoint emulator
```

Endpoints: `state` (full snapshot: page, inputMode, focusWidget, navigation, geometry), `navigation` (homeGrid tiles, hoveredIndex), `input` (mouse/controller mode, pressed buttons), `emulator` (type, running, paused, frameNumber).

Use `boot --no-focus` to launch without stealing focus from the user's current work.

## Standard Flow

1. Decide whether source inspection or visual capture is necessary.
2. If artifacts already exist, inspect them and stop.
3. If a one-shot capture is enough, use `snapshot`.
4. If interaction is required, `boot`, navigate, `screenshot`, judge immediately, and continue only if the evidence is still inconclusive.
5. After every screenshot, judge it before taking another one.
6. Stop after a definitive pass or fail, or after three navigation attempts.
7. `kill` the app at the end of an interactive session.

## Fast Rules

- Do not read `.png` or `.ppm` files as text.
- Screenshot capture does not need focus.
- Only focus before input when focus is actually required.
- `snapshot` is the fast path for non-interactive checks.
- `session --plan` is the fast path for known interactive paths.
- Report pass, fail, or blocked with evidence and the smallest actionable follow-up.
