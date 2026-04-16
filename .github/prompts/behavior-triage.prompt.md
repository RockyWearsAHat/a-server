---
description: "Triage a UI behavior issue — determine root cause and produce a fix plan."
agent: Project Lead
argument-hint: "Describe the misbehavior: what you expected, what actually happened, which screen, and any reproduction steps."
---

`Project Lead` should triage the behavior issue by subsystem:

- **Visual / styling** → dispatch to `Senior Engineer` for QSS/widget investigation
- **Navigation / focus** → dispatch to `Senior Engineer` with focus-and-behavior checklist context
- **Emulator rendering** → dispatch to `Senior Engineer` with emulator-core instruction context
- **Crash / signal** → follow the runtime-debugging instruction workflow

Include:

- the screen or component where the issue occurs
- expected vs actual behavior
- reproduction steps (navigation path, input sequence)
- any error output, debug.log excerpts, or screenshots
