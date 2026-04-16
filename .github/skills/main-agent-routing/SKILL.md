---
name: main-agent-routing
description: "Request parsing, delegation, brief writing, and context management for the main coordinator agent."
user-invocable: false
---

# Main Agent Routing

Load this skill when delegating work to Project Leads or Senior Engineers.

## Request Parsing (BEFORE any delegation)

1. Read the user's full message. Identify EVERY distinct task — not just the one mentioned most.
2. **Identify the action type for each task.** The user's verb matters:
   - "implement", "add", "build", "change", "switch to" → **implement** (code changes expected)
   - "fix", "debug", "crash", "broken", "segfault", "failing", "not working" → **debug-fix** (dispatch directly to Senior Engineer, skip Project Lead)
   - "research", "investigate", "document", "audit", "analyze" → **research-only** (NO code changes — knowledge docs only)
   - "verify", "test", "check if working", "playtime testing" → **runtime-verify** (dispatch to Senior Engineer with ROM paths and verification criteria)
   - When ambiguous, ask the user. Do NOT default to implementation.
3. Group into independent domains (e.g., "YouTube playback", "streaming services TV UX", "screen mirror").
4. For `implement` and `research-only`: create one `Project Lead` brief per domain.
5. For `debug-fix`: dispatch directly to `Senior Engineer` with symptom, context, and domain. Skip Project Lead.
6. For `runtime-verify`: dispatch directly to `Senior Engineer` with ROM paths from `~/Desktop/ROMs/` (GBA: `.gba`, PS1: `.bin/.cue` in subdirs, Switch: `.xci`), emulator type, and criteria.

## Hierarchy

```
Main Agent (coordinator — product knowledge, parallel delegation, user communication)
  ├── [debug-fix] Senior Engineer (direct dispatch — iterative fix loop)
  │     ├── Code Engineer(s) (implementation)
  │     ├── Test Engineer (verification)
  │     └── Visual Engineer (verification)
  └── [implement/research] Project Lead(s) (planner/dispatcher)
        ├── R&D Lead (external docs and design research)
        ├── Senior Engineer (implementation + verification gate)
        │     ├── Code Engineer(s) (implementation)
        │     ├── Test Engineer (verification)
        │     └── Visual Engineer (verification)
        └── Quality Auditor (final standards check)
```

## Parallel Delegation

- Make ALL `runSubagent` calls for Project Leads in a SINGLE parallel tool-call block. Sequential dispatch wastes context.
- No dependencies between leads. Call them all at once.
- Each Project Lead gets a **self-contained, comprehensive brief**. No transcript dumps or pre-read source files.
- Track which leads have reported back. Do NOT hand back to the user until ALL complete.

## Brief Writing

The main agent knows WHAT features exist (product overview), not HOW they're implemented. Briefs describe desired user-facing outcomes, not implementation details.

**Parity rule**: Each brief must match the quality of a solo-task brief. Multi-task batches don't justify thinner briefs.

**Constraint fidelity**: If the user says "don't edit X", "research only", or "just investigate", that constraint appears VERBATIM in the brief's Constraints field AND as the Action Type. Dropping a user constraint is a critical failure. Do NOT invent constraints the user didn't state — domain scope already bounds the lead.

Every brief MUST include:

1. **Action type** — `implement` or `research-only` (first field — controls everything downstream)
2. **Goal** — User-facing outcome in concrete terms
3. **Context** — Relevant background for the domain. Verify claims from code or Explore results — do not recite assumptions from memory.
4. **Domain scope** — Which product area this covers and its boundaries
5. **Constraints** — Quality bar, non-goals, user restrictions VERBATIM. Don't list unrelated domains as negative scope.
6. **Expected outcome** — What "done" looks like
7. **Research directive** — "Do thorough research before planning. Start from official developer documentation."
8. **Output path** — `"Write your results to /memories/session/pl-<domain>-result.md"`

## Context Management

- **Zero-return pull model**: Subagents write results to session memory paths. They return NOTHING inline.
- Include output paths in every dispatch: `pl-<domain>-result.md` or `se-<domain>-result.md` for debug-fix.
- After ALL leads complete and final build passes, delete ALL `/memories/session/` files. Every request starts and ends clean.
- Do NOT re-escalate or restart when context gets tight. Checkpoint to session memory and continue.

## Rules

- Trivial tasks (factual answer, one-line fix): work directly.
- Debug-fix: dispatch directly to Senior Engineer with symptom and logs. Skip Project Lead.
- Runtime-verify: dispatch to Senior Engineer with ROM paths and criteria.
- Multi-step work: delegate to Project Lead(s).
- After all leads report: run `make build` as final sanity check, then report.
- If a request changes durable repo facts, update the relevant `.github/knowledge/` note or human-facing doc. Update `copilot-instructions.md` only when the always-on runtime baseline itself changed.
- Don't hand back until EVERY lead completes. Resolve failures before closing.
- Don't run the full test suite as a final step. Leads verify their own scope.
