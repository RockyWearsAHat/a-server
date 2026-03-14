---
description: "Host-driven visual development loop facts."
applyTo: "scripts/visual_dev_loop.py,.github/agents/visual-engineer.agent.md,.github/prompts/visual-dev-tester.prompt.md"
---

# Visual Development Loop

This instruction file is the short companion to the visual loop skills.

- Control the app with `python3 scripts/visual_dev_loop.py` terminal commands.
- Judge images with the vision MCP tools, not with terminal `analyze` output.
- Do not read `.png` or `.ppm` files as text.
- `snapshot` is the fast path for one-shot checks; `session --plan` is for known multi-step navigation.
- Only use `focus` before input when focus is actually required. Screenshot capture does not need focus.
- Use `boot --no-focus` to launch without stealing focus from the user, always preferred.
- Use `poll-state --endpoint state|navigation|input|emulator` to verify app state before capturing screenshots.
- State polling returns JSON with page name, focused widget, input mode, navigation position, emulator status.
- `--no-activate` CLI flag on AIOServer prevents window from stealing focus on startup.

## Visual Review Philosophy

The vision audit is the **designer sitting down with the engineer**. It is the end-user, the play-tester, the creative director reviewing the build. Treat each audit as a comprehensive design review — not a quick bug check.

### Audit call structure

- **Context**: Brief description of what the UI is, what changed, and what tech is used.
- **Goal**: Ask the reviewer to act as a senior product designer evaluating a consumer TV app. Request a single numeric score (1-10) and a **prioritized punch-list** of concrete fixes — not vague suggestions. Each item should name the element, state what's wrong, and say exactly what to do.
- **Do NOT**: Ask yes/no questions, request 10-point rubrics, or give the reviewer a checklist to fill in. Let them look with fresh eyes and tell you what jumps out.

### Efficiency rules

- Vision calls are expensive. One audit per **milestone** (5-10 changes batched), not per tweak.
- After receiving feedback, implement **every actionable item** in one batch, build once, then audit again.
- Skip audits for mechanical changes (a color value, a spacing constant). Only audit when the visual output meaningfully changed.
- The loop: **Audit → identify all issues → implement all fixes → build → audit**. Repeat until the score target is met.
