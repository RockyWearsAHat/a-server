---
name: emulator-platform-onboard
description: "Wire an emulator core into launch flow with full verification and acceptance gating."
user-invocable: false
---

# Emulator Platform Onboard

Use this skill when a core exists but is not fully launchable from the product flow.

## Onboarding Steps

1. Register platform in `EmulatorFormats.h` with display name, extensions, launchability, and badge metadata.
2. Verify game discovery/filter integration through `GamesLibraryPage` and related adapters.
3. Ensure launch path wiring resolves to the correct emulator system constructor.
4. Add or update targeted tests and CMake registration for the new platform path.
5. Run headless/runtime smoke checks.
6. Add determinism coverage (Layer 4) if supported; otherwise document the explicit gap.
7. Update `.github/COMPLETION_CHECKLIST.md` and platform knowledge docs.

## Required Verification

- Always run `.github/skills/emulator-verification-pipeline/SKILL.md` with change type `New platform launch wire-up`.
- Platform onboarding is not complete until Layers 0-7 pass, or an approved temporary gap is documented with owner + follow-up.

## Reporting Contract

Return:

- Files changed
- Test coverage added
- Verification layers executed and outcome
- Any temporary gaps (with owner and follow-up path)
