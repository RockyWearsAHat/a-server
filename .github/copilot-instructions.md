# AIO Entertainment System — Copilot Instructions

This file is intentionally short. The detailed, structured guidance lives in `.github/instructions/`.

## Model Selection

- **Default model**: GPT-4.1 (general tasks, documentation, code review)
- **Plan agent** (`@Plan`): Claude Opus 4.5 (complex research and planning)
- **Implement agent** (`@Implement`): Raptor Mini (execution and implementation)

## Agents

- **`@Plan`** — Diagnose → Write EXACT CODE to `.github/plan.md`. Never implements directly.
- **`@Implement`** — Execute `.github/plan.md` step-by-step. Never improvises or interprets.

**Workflow:** User request → `@Plan` (diagnose + write code plan) → `@Implement` (execute exactly)

## Golden Rules

1. **Documentation-first TDD**: update docs/spec → write tests that mirror the docs → implement.
2. **Spec-driven emulation**: prefer GBATEK + project docs over "what other emulators do".
3. **No hacks**: game-specific fixes only if they replicate real hardware/BIOS behavior.
4. **Keep code clean**: small diffs, clear names, consistent style, and documentation updates.
5. **Clean only after user verification**: once the user confirms the issue is solved, clean workspace artifacts.
6. **Plans contain CODE**: `@Plan` writes exact code blocks, not prose. `@Implement` copy-pastes.

## Canonical Docs (read first)

- `.github/instructions/memory.md` — living “memory” of the codebase (update alongside changes)
- `.github/instructions/tdd.md` — documentation-first TDD workflow (tests mirror docs)
- `.github/instructions/workspace-hygiene.md` — what to delete, when to delete it
- `.github/instructions/code-style.md` — conventions (namespaces, includes, logging, comments)

## Repo Constraints

- Build outputs live under `build/` and are regeneratable. **Never edit generated build files**.
- Prefer the centralized logger (`AIO::Emulator::Common::Logger`) so logs are captured consistently.
- Keep all code within `AIO::*` namespaces (no `using namespace`).

## Build / Test (macOS)

- VS Code task: **Build** (`make build`)
- VS Code task: **Test** (Build → CTest)
- Focused executables: `./build/bin/CPUTests`, `./build/bin/EEPROMTests`

## Cleanup

- Use `./scripts/clean.sh`.
- Default run keeps `.sav` and `dumps*` unless explicitly requested.
- Do **aggressive** cleanup only after user confirms the problem is solved.
