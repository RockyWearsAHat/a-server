---
description: "Onboard an emulator core into launch flow and verify it through acceptance gates."
agent: Project Lead
argument-hint: "Provide platform name, current status, launch path gaps, and known test assets."
---

`Project Lead` should execute platform onboarding using `.github/skills/emulator-platform-onboard/SKILL.md` and verify through `.github/skills/emulator-verification-pipeline/SKILL.md`.

Acceptance criteria:

- required wiring steps are complete
- verification layers pass for change type `New platform launch wire-up`
- `.github/COMPLETION_CHECKLIST.md` reflects the new platform status
