---
name: visual-development-tester
description: "Testing, planning, and judgment methodology for the Visual Development Tester agent."
user-invocable: false
---

# Visual Development Tester

Use this skill only as a lightweight dispatch layer.

- Prefer source inspection when code can answer the question.
- If rendered output must be judged, use `visual-development-loop` for the actual workflow.
- If a failure is found, return the evidence plus a compact implementation plan.
