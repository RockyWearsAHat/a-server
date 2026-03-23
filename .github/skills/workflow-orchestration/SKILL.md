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
- Every delegating agent (Main Agent, Project Lead, Senior Engineer) MUST delegate to their designated subagents.
- Project Lead never implements. Senior Engineer delegates to Code Engineer for all non-trivial work.
- If a subagent call fails due to a transient error, retry once. If it fails again, report the specific error — do not absorb the work yourself.

## Brief Anti-Inclusion Rule

When writing briefs, do NOT include: file paths, code snippets, search results, or transcript history. Briefs describe outcomes, not implementation details.

## Research Gate (Project Lead Phase 1)

At least one `Explore` subagent call is required before producing an implementation plan. If the plan does not reference specific findings from Explore or R&D Lead, the research phase was skipped.

**Anti-pattern**: Skipping research and immediately delegating to `Senior Engineer` with only the brief's goal restated. This is the #1 failure mode in multi-lead dispatch.

## Stop Conditions

- Stop when the requested change is implemented and verified.
- Stop when the answer is definitive.
- Stop and escalate when requirements conflict or the missing information is user-owned.
- Do not keep delegating after success just to add ceremony.

## Context Survival Protocol — Zero-Return Pull Model

Running out of context is catastrophic — all work in that agent's session is lost, and the cascade failure destroys everything the hierarchy built below it. The #1 cause of context death is subagent responses flowing back into the parent's conversation. **The solution: subagents never return results inline. They write to session memory. The parent reads on its own terms.**

### Tool result leaks

Built-in file edit tools (`replace_string_in_file`, `multi_replace_string_in_file`, `create_file`) generate diff thumbnails that flow back into the PARENT agent's conversation context — even through the zero-return model. This is a platform behavior that instructions cannot override.

**The fix**: Agents that edit files (Code Engineer, Test Engineer) must NEVER use built-in edit tools. They edit through the terminal using Python scripts. Their `tools` frontmatter must NOT include `"edit"`. This is enforced in each agent's frontmatter.

### The protocol

1. **Upstream agent dispatches with a predefined output path**: When calling a subagent, include the session memory path where results go. Example: `"Write your results to /memories/session/se-task1-result.md"`
2. **Downstream agent writes results to that path and ends**: The subagent does all its work, writes a structured result file to the specified path, then finishes. It returns NOTHING to the parent.
3. **Upstream agent reads the file, then deletes it immediately**: After the subagent returns, the parent reads the result file using the memory tool, extracts what it needs, then **deletes the file**. Result files are single-use — once read, they serve no purpose and waste disk space. This keeps the session memory footprint minimal throughout the entire flow.
4. **Checkpoints overwrite, never accumulate**: When writing a self-checkpoint, always overwrite the same file path (e.g., `/memories/session/pl-checkpoint.md`). Never create `pl-checkpoint-1.md`, `pl-checkpoint-2.md`, etc. Only the latest state matters.

### Path naming convention

Use descriptive, collision-free paths:

- `/memories/session/<role>-<task>-result.md` — subagent results (deleted after parent reads)
- `/memories/session/<role>-checkpoint.md` — self-checkpoints (overwritten each phase, ONE file per role)

Examples:

- `/memories/session/se-homepage-polish-result.md`
- `/memories/session/ce-fix-tile-spacing-result.md`
- `/memories/session/te-homepage-tests-result.md`
- `/memories/session/pl-phase1-checkpoint.md`
- `/memories/session/rd-airplay-research-result.md`

### Why this works

- Subagent results NEVER enter the parent's conversation context automatically. The parent is in full control.
- Result files are deleted immediately after reading — disk usage stays flat regardless of how many subagent round-trips occur.
- Checkpoints overwrite a single file — disk usage per delegating agent is exactly ONE file, not one per phase.
- If the parent's context is getting tight, it can read just the status line from a result file before deleting it.
- Knowledge docs (`.github/knowledge/`) store PERMANENT findings. Session memory stores TEMPORARY results. Clean separation.

### Self-checkpointing

Delegating agents (Project Lead, Senior Engineer, R&D Lead) write their own checkpoints to session memory at phase boundaries. This externalizes state so remaining context isn't wasted holding historical decisions:

- ONE checkpoint file per role: `/memories/session/<role>-checkpoint.md`
- Each phase boundary OVERWRITES the same file. Never create numbered variants.
- Read the checkpoint back when you need previous state instead of scrolling conversation history.

### Disk management during the flow

- **Read-then-delete**: Every time an upstream agent reads a subagent result file, it deletes the file immediately after. No exceptions.
- **Overwrite-only checkpoints**: One file per role, overwritten each phase. Never accumulate.
- **Maximum session files at any point**: The number of active subagents + the number of delegating agents. For a typical Project Lead flow: 1 PL checkpoint + 1 SE result being written = 2 files max.
- **Knowledge docs are the durable store**: Any finding that needs to persist beyond the current dispatch goes to `.github/knowledge/`, not session memory.

### Cleanup rules

- **Read-then-delete is the primary mechanism** — most files are already gone by the time work completes.
- **Final sweep**: Before writing its own result file, each delegating agent checks `/memories/session/` and deletes any orphaned files from its subagents.
- **Main Agent**: After all leads complete and final build passes, delete ALL remaining `/memories/session/` files including its own.
- **Rule**: NEVER leave session files behind. Every request starts and ends with a clean `/memories/session/`.
