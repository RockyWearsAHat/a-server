---
name: visual-development-tester
description: "Testing, planning, and judgment methodology for the Visual Development Tester agent."
user-invocable: false
---

# Visual Development Tester

Use this skill only as a lightweight dispatch layer.

- Prefer source inspection when code can answer the question.
- For emulator correctness requests, route through `.github/skills/emulator-verification-pipeline/SKILL.md` first and then use visual checks as Layer 6 evidence.
- If rendered output must be judged, use `visual-development-loop` for the actual workflow.
- If a failure is found, return the evidence plus a compact implementation plan.

## Reporting Format

Every visual test result must use the structure defined in `visual-audit.instructions.md`. The 7 weighted categories and output format below are authoritative.

### Result: [PASS | FAIL | NEEDS_WORK]

**Stage 0 — Token Compliance**

- Violations: [list each violation, or "None"]
- Deductions: [total, or 0]
- Score ceiling applied: [Yes (max 70) | No]

**Category Scores** (each 0–10, final = Σ(category × weight × 10)):

| Category            | Weight | Score | Issues                   |
| ------------------- | ------ | ----- | ------------------------ |
| Layout & Alignment  | 20%    | X/10  | [list issues, or "None"] |
| Typography          | 15%    | X/10  | [list issues, or "None"] |
| Spacing & Rhythm    | 15%    | X/10  | [list issues, or "None"] |
| Visual Hierarchy    | 15%    | X/10  | [list issues, or "None"] |
| Color & Contrast    | 10%    | X/10  | [list issues, or "None"] |
| Component Quality   | 15%    | X/10  | [list issues, or "None"] |
| Professional Polish | 10%    | X/10  | [list issues, or "None"] |

**Critical Failures:** [none | list — any trigger caps score at 75]
**Design System:** [strong | moderate | weak]
**Focus Navigation:** [clear | unclear | N/A]

**Final Score:** [total]/100
**Rating:** [AAA Production | Professional | Semi-Professional | Fail]
**Gate:** [PASS ≥ 90 | FAIL < 90]

**Issues (prioritized, if FAIL):**

1. [element] — [what's wrong] — [exact fix: file → selector → property → target value]
2. ...
