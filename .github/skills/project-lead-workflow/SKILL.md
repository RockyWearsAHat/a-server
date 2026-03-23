---
name: project-lead-workflow
description: "Planning, research, dispatch, and review methodology for the Project Lead agent."
user-invocable: false
---

# Project Lead Workflow

Your most important resource is your remaining context. Every token wasted on verbose reports is a token you cannot use for final review, re-dispatch, or problem-solving.

## Context Preservation

- **Zero-return pull model**: All subagents write results to predefined session memory paths. They return NOTHING inline. You read their result files, then delete them immediately.
- **Dispatch with output path**: Every subagent dispatch includes the session memory path:
  - Senior Engineer: `"Write your results to /memories/session/se-<task>-result.md"`
  - R&D Lead: `"Write your results to /memories/session/rd-<topic>-result.md"`
  - Quality Auditor: `"Write your results to /memories/session/qa-<scope>-result.md"`
- **Read-then-delete**: After reading any result file, delete it immediately. Single-use.
- **One checkpoint file, overwritten each phase**: `/memories/session/pl-checkpoint.md`. Never create numbered variants.
- **Budget across phases**: Research (Phase 1) should not consume more than a third of your context.
- **If context gets tight late**: Skip Quality Auditor and do a targeted self-review of highest-risk changes.
- **Final cleanup**: Delete your checkpoint and orphaned files before writing your result.

## Phase 1: Research (MANDATORY)

You MUST complete thorough research before creating any implementation plan.

1. **Check the knowledge cache**: Search `.github/knowledge/` for existing research. Use `Explore` to search and read cached notes. Start here — don't repeat done work.
2. **Read the source map**: `.github/knowledge/source-map.md` to locate relevant files.
3. **Explore the current implementation**: Delegate to `Explore`. **At least one Explore call is required** — this is the minimum research gate.
4. **Research unknowns**: Delegate to `R&D Lead` when the task involves external APIs, protocol specs, or design tradeoffs. Explicitly instruct R&D Lead to prioritize official developer documentation.
5. **Identify the full scope**: Map every file, component, and interaction that changes will touch.

**Research gate**: Phase 1 is not complete until you have received results from at least one Explore delegation. Your Phase 2 plan must reference specific findings from research.

**Cache confidence**: Cached knowledge is a helpful starting point, not guaranteed truth. When cached notes cite community sources, treat them as leads to verify.

## Phase 2: Plan and Dispatch

**Check the Action Type from your brief FIRST.**

### If Action Type = `implement`

- Write a concrete implementation plan grounded in Phase 1 findings.
- Include explicit acceptance criteria. Prefer concrete, testable outcomes over vague success language.
- Classify the task by risk before dispatching:
  - Critical: emulator correctness, startup/shutdown, persistence, protocol behavior
  - High: Qt ownership, threading, shared navigation, build graph changes
  - Medium: page or feature behavior inside an existing subsystem
  - Low: tests, docs, narrow refactors
- Delegate to Senior Engineer with: what exists now, what needs to change, which files, and specific acceptance criteria.
- Senior Engineer owns implementation AND verification. It loops internally until verified.
- You do NOT separately dispatch Test Engineer or Visual Engineer — Senior Engineer has them.
- For native C++/Qt/CMake work, tell Senior Engineer to load `native-cpp-workflow` and the matching scoped instruction files.

### If Action Type = `research-only`

- Do NOT dispatch Senior Engineer for code changes. No source files may be edited.
- Deliverable is a knowledge document in `.github/knowledge/`.
- Delegate to Explore and/or R&D Lead for deeper research.
- Synthesize findings into a structured document.
- Skip Phases 3 and 4.

## Phase 3: Receive and Evaluate

- Read Senior Engineer's result file from session memory.
- If blockers: adjust the plan and re-dispatch.
- If complete and verified: proceed to Phase 4.
- Multiple SEs for independent sub-tasks are fine. Read each result after all complete.

### Evaluation checklist

- Did the result satisfy every acceptance criterion from the plan?
- Did the verification level match the risk tier?
- If the task was native-code heavy, did the result confirm build plus the smallest relevant test scope?
- Did the implementation stay within the approved problem and avoid opportunistic expansion?

## Phase 4: Final Comprehensive Review

- Delegate to Quality Auditor for a final standards check.
- Review aggregate result against the original request: is every part actually fixed?
- If the auditor or your review finds issues: re-dispatch Senior Engineer with targeted fixes.
- Report completion only when every part is implemented, verified, and tested.
