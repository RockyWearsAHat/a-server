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

## Reporting Format

Every visual test result must use this structure:

### Result: [PASS | FAIL | NEEDS_WORK]

**Stage 0 — Token Compliance**

- Violations: [list each violation, or "None"]
- Deductions: [total, or 0]
- Score ceiling applied: [Yes (max 70) | No]

**Stage 1 — Critical Failures:** [score/20 or "n/a"]

- [list issues, or "None"]

**Stage 2 — Composition:** [score/20 or "n/a"]

- [list issues, or "None"]

**Stage 3 — Typography:** [score/15 or "n/a"]

- [list issues, or "None"]

**Stage 4 — Polish:** [score/15 or "n/a"]

- [list issues, or "None"]

**Stage 5 — Motion/Interaction:** [score/10 or "n/a"]

- [list issues, or "None"]

**Final Score:** [total]/100
**Gate:** [PASS ≥ 90 | FAIL < 90]

**Recommended fixes (if FAIL):** [ordered list of highest-impact fixes]
