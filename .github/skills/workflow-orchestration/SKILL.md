---
name: workflow-orchestration
description: "Context budgets, delegation enforcement, and stop conditions for AIO Server workflow."
user-invocable: false
---

## Context Budget Rules

- Main agent: product overview + routing only. No source files.
- Project Lead: research, plan, delegate, review. Never implements.
- Senior Engineer: delegates to Code Engineers, reviews their output, confirms build. Implements only trivial one-liners.
- Code Engineer: reads and edits source files, runs targeted builds/tests.
- Each level gets progressively more implementation detail. Never push detail upward.

## Delegation Enforcement

- Subagents CAN invoke other subagents. Nested delegation works.
- Every delegating agent MUST delegate to their designated subagents.
- Project Lead never implements. Senior Engineer delegates to Code Engineer for all non-trivial work.
- If a subagent call fails due to a transient error, retry once. If it fails again, report the specific error — do not absorb the work yourself.

## Brief Anti-Inclusion Rule

Briefs describe outcomes, not implementation details. Do NOT include: file paths, code snippets, search results, or transcript history.

## Research Gate

At least one Explore subagent call is required before producing an implementation plan. If the plan does not reference specific findings from Explore or R&D Lead, the research phase was skipped.

**Anti-pattern**: Skipping research and immediately delegating to Senior Engineer with only the brief's goal restated. This is the #1 failure mode.

## Stop Conditions

- Stop when the requested change is implemented and verified.
- Stop when the answer is definitive.
- Stop and escalate when requirements conflict or the missing information is user-owned.
- Do not keep delegating after success just to add ceremony.

## Zero-Return Pull Model

Subagents never return results inline. They write to session memory. The parent reads on its own terms.

### Tool result leaks

Built-in edit tools generate diff thumbnails that flow into parent context. Agents that edit files (Code Engineer, Test Engineer) must edit through the terminal, not built-in tools. Their `tools` frontmatter must NOT include `"edit"`.

### Protocol

1. Upstream dispatches with a predefined output path: `"Write your results to /memories/session/se-task1-result.md"`
2. Downstream writes results to that path and returns NOTHING.
3. Upstream reads the file, then deletes it immediately. Single-use.
4. Checkpoints overwrite one file per role (e.g., `/memories/session/pl-checkpoint.md`). Never create numbered variants.

### Path naming

- `/memories/session/<role>-<task>-result.md` — subagent results (deleted after read)
- `/memories/session/<role>-checkpoint.md` — self-checkpoints (overwritten each phase)

### Cleanup

- Read-then-delete is the primary mechanism.
- Final sweep: each delegating agent deletes orphaned subagent files before writing its own result.
- Main Agent deletes ALL `/memories/session/` files after completion. Every request starts and ends clean.
