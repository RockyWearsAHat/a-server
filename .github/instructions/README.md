# AIO Copilot Instruction Set

This folder contains the _canonical_ instructions for working in this repository.

## Files

- `memory.md`

  - The living, curated “memory” of the codebase: architecture, file map, invariants, debugging notes, and known pitfalls.
  - This is designed to reduce repeated rescans of the repo.

- `tdd.md`

  - Documentation-first TDD: docs/spec → tests that mirror docs → implementation.
  - Optimized for emulator correctness and regression-proofing.

- `workspace-hygiene.md`

  - What counts as “artifact”, what is safe to delete, and _when_ to delete.
  - Includes the rule: **only run aggressive cleanup after user verification**.

- `code-style.md`
  - Naming, structure, logging, comments, Qt conventions, and documentation expectations.

## Agents (in `.github/agents/`)

- **Plan** — Research and outline multi-step plans before implementation.
- **Implement** — Execute planned features with documentation-first TDD.

## How to use

1. Before any non-trivial change: read `memory.md` and the relevant doc(s) under `docs/`.
2. For complex work: use the **Plan** agent first to outline the approach.
3. Update docs/spec first, then add/update tests.
4. Use the **Implement** agent (or work manually) to make the smallest correct change.
5. Run the narrowest test set first, then expand.
6. After the user confirms the issue is solved: run cleanup per `workspace-hygiene.md`.
