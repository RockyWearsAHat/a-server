---
name: workflow-orchestration
description: "Shared workflow-first routing rules for fast Copilot execution in AIO Server."
user-invocable: false
---

# Workflow Orchestration

Use this skill for multi-step agent work. Optimize for elapsed time and low unnecessary token spend, not for elaborate roleplay.

## Triage

- Work directly when one agent can complete the task safely.
- Delegate only when specialization changes the outcome or materially speeds up execution.
- Skip research if the task is implementation-ready.
- Skip extra coordination layers unless the task truly has parallel tracks.

## Direct Vs Delegate

- Direct path: scoped code fix, small test update, focused explanation, or local file edit.
- Delegate to `Code Engineer` for implementation.
- Delegate to `Test Engineer` for targeted verification or new regression coverage when independent test work is needed.
- Delegate to `Visual Engineer` for rendered-output evidence.
- Delegate to `R&D Lead` only for unresolved design or external-doc questions.
- Use `Senior Engineer` only for large parallel delivery across code, tests, and visuals.
- Use `Quality Auditor` only when the workflow is churning, repeating, or drifting.

## Context Gathering

- Gather read-only context once.
- Parallelize independent searches and file reads.
- Do not send one agent to reread context another agent already summarized unless the original evidence is missing.
- Hand off only the files, constraints, and expected result needed for the next step.

## Compact Handoffs

Every handoff should fit this shape:

1. Goal
2. In-scope files or subsystem
3. Constraints and non-goals
4. Required verification
5. Expected output format

Do not paste long transcript history when a short brief will do.

## Stop Conditions

- Stop when the requested change is implemented and verified.
- Stop when the answer is definitive.
- Stop and escalate when requirements conflict or the missing information is user-owned.
- Do not keep delegating after success just to add ceremony.

## Escalate Model Cost Only When Needed

- Default to the workspace model or a lower-cost model for local code reading, editing, and focused verification.
- Escalate to a stronger model for ambiguous architecture decisions, repeated failed attempts, conflicting evidence, or large high-risk refactors.
- Do not pin premium models as the default path for routine work.
