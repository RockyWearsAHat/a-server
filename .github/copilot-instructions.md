Native C++ emulator (GBA, PS1) with Qt 6 UI, a CMake build configured through the top-level Makefile, and a server/backend layer.

Build: `make build`
Test: `cd build/generated/cmake && ctest --output-on-failure`

Architecture:

- Emulator cores: `src/emulator/` + `include/emulator/` (`gba/`, `ps1/`)
- GUI shell: `src/gui/` + `include/gui/`
- Styling: `assets/qss/` (`youtube.qss`, `tv.qss`)
- Server: `server/`
- Tests: `tests/` (GoogleTest, one binary per subsystem)

Core constraints:

- Fix root causes. Never mask failures or broaden tolerances.
- Keep widget object names, dynamic properties, and QSS selectors synchronized.
- `QssValidator` runs at build time; a successful build validates QSS.
- Keep changes aligned with the checked-in Makefile to CMake to Ninja flow.

Routing policy:

- Prefer the direct path. If one worker can finish the task safely, do not add extra coordinators.
- Delegate only for specialization: `Code Engineer` for implementation, `Test Engineer` for tests and verification, `Visual Engineer` for rendered-output checks, `R&D Lead` for optional research, `Quality Auditor` for stalls or churn.
- `Senior Engineer` is optional for unusually broad parallel work, not the default implementation hop.
- Gather read-only context once, in parallel when possible, then act. Do not bounce the same file reads through multiple agents.
- Use compact handoffs: goal, files, constraints, expected output. Do not forward long transcript dumps.
- Stop once the requested change is implemented and verified. Do not keep delegating after a definitive answer.

Model policy:

- Use the workspace default or a low-cost model for routine reads, code edits, and targeted verification.
- Escalate to a stronger model only for ambiguous architecture choices, repeated failed attempts, conflicting evidence, or high-risk multi-file refactors.
- Do not pin premium models by default when the task is narrow and evidence is local.

Agent mode:

- For small tasks, work directly.
- For multi-step work, load the shared orchestration skill and choose the fewest agents needed.
- If research is not required, skip it.
- If the task is code plus nearby tests, prefer one implementation pass over serial code-then-test delegation unless independent verification is needed.

Iteration discipline:

- Work like a developer: batch related fixes, build once, check once. Do not micro-iterate one change at a time.
- Implement changes in parallel across all affected areas at once — that is the AI advantage over a human coder. Use it.
- The visual development loop is: **audit → identify all issues → implement every fix in one batch → build → audit**. Repeat.
- Vision audit calls are expensive. One per milestone (5-10 changes), never per tweak.
- The vision audit is the designer/playtester reviewing your build. Give it comprehensive context and let it judge freely — then act on every item it raises.
- Skip visual audits for mechanical changes (color values, spacing). Only audit when visual output meaningfully changed.
- Prefer launching the app for the user to preview live when the changes are small or exploratory.

If new directives come up that affect general project quality, keep the copilot documentation for yourself updated in accordance with the project standards. We should prioritize short concise instructions that are easy to follow and check against, rather than long detailed ones that are hard to parse, but these should also be consistently and constantly kept accurate to the project expectations and guidelines given directly by the user.
