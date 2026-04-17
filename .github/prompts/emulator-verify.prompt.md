---
description: "Run layered emulator verification for a platform or ROM with explicit pass/fail gates."
agent: Project Lead
argument-hint: "Provide: platform, ROM, what changed, expected behavior, and whether baseline updates are allowed."
---

`Project Lead` should run emulator verification using `.github/skills/emulator-verification-pipeline/SKILL.md`.

Include in the result:

- change type and required verification layers
- commands executed per layer
- pass/fail per layer with evidence
- blockers and exact next fix step
- whether deterministic baselines were updated
