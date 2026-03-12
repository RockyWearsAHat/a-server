---
description: "Accuracy, testing, and logging rules for emulator-core changes."
applyTo: "src/emulator/**/*.cpp,include/emulator/**/*.h,include/emulator/**/*.hpp,tests/**/*Tests.cpp"
---

# Emulator Core Workflow

- Fix root causes before adjusting timings, masking failures, or broadening tolerances.
- Preserve existing timing and correctness behavior unless the task explicitly changes emulation semantics.
- Prefer focused tests, characterization tests, or deterministic headless runs before broad manual verification.
- Keep hot-path logging minimal and use targeted trace flags plus `debug.log` when runtime evidence is needed.
