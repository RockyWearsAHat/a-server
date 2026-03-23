---
name: senior-engineer-workflow
description: "Implementation management, diff review, verification, and debug-mode protocol for the Senior Engineer agent."
user-invocable: false
---

# Senior Engineer Workflow

## Debug Mode (direct dispatch from main agent)

When dispatched directly for a `debug-fix` task:

1. **Load cached knowledge first**: Read the relevant architecture doc from `.github/knowledge/` (index in `source-map.md`). The cache has subsystem architecture, file locations, known issues. This is your warm start.
2. **You own the full loop**: read code → form hypothesis → dispatch Code Engineer → run tests → interpret → iterate. No Project Lead ceremony.
3. **Start from the symptom**: Combine the bug report with cached knowledge to locate code quickly.
4. **Pass context forward**: Include knowledge cache facts in Code Engineer and Test Engineer dispatches. Tell them to include actual error messages, stack traces, and code references.
5. **Report diagnostics upward**: Root cause, what changed, and verification evidence with log lines.
6. **Iterate fast**: Use context from previous attempts. Don't re-explore from scratch.
7. **Write back what you learned**: Update `.github/knowledge/` after the fix. Use this format:

```
## [Bug Title] (YYYY-MM-DD)
- **Symptom**: what the user reported or what failed
- **Root cause**: what actually broke and why
- **Fix**: what changed, which files
- **Verification**: which tests confirm it, build status
```

### Crash Signal Reference

| Exit | Signal  | Meaning                    | First Step                                     |
| ---- | ------- | -------------------------- | ---------------------------------------------- |
| 139  | SIGSEGV | Null/bad pointer deref     | Check last debug.log line, find the deref      |
| 134  | SIGABRT | Assertion / abort()        | Read assertion message in stderr or debug.log  |
| 136  | SIGFPE  | Division by zero / FP      | Find the arithmetic in the call path           |
| 137  | SIGKILL | OOM or external kill       | Check memory usage, large allocations          |
| 0    | —       | Clean exit                 | Not a crash — check logic for early exit       |

### Diagnosis Protocol

1. Launch with `AIO_LOG_LEVEL=debug` and reproduce.
2. Read `debug.log` (project root). Last lines before crash show the active subsystem.
3. Filter: `grep -i "<subsystem>\|crash\|error\|fatal" debug.log`
4. Use source map to locate the code.
5. Form hypothesis from signal + log + code path, dispatch Code Engineer with a specific fix target.

### Common Crash Patterns

- Launch crash → constructor/initialization issue (MainWindow setup, boot services)
- Page navigation crash → page constructor, null widget pointers, signal/slot wiring during creation
- Emulator crash → emulation loop bounds, unimplemented opcodes, memory access violations
- WebEngine crash → missing init, invalid URLs, resource exhaustion, Qt process model

### When Unit Tests Aren't Enough

Object lifecycle (Qt parent-child ownership), signal/slot threading, resource init order, WebEngine process model — for these, log → locate → hypothesize → fix → relaunch is primary verification.

## Standard Mode (dispatched by Project Lead)

- Break the plan into clear tasks and delegate to Code Engineer(s).
- Consume the approved plan and existing knowledge; don't broaden into open-ended research.
- Use Explore to check `.github/knowledge/` for cached facts before asking PL for clarification.
- Handle only trivial one-line fixes directly.
- For C++/Qt/CMake tasks, load `native-cpp-workflow` before dispatching implementation.

## Acceptance Criteria and Risk

- Translate the Project Lead brief into concrete acceptance criteria before dispatch.
- Match verification depth to risk:
	- Critical: build plus targeted tests and runtime confirmation where applicable
	- High: build plus targeted tests, and visual/runtime checks if lifecycle or UI is involved
	- Medium: build plus targeted tests or visual confirmation
	- Low: smallest relevant verification only
- Do not sign off work that lacks explicit proof for the relevant risk tier.

## Diff Review (MANDATORY after every Code Engineer delivery)

Treat every delivery like a pull request:

1. **Behavioral correctness**: Does the code DO what it claims? Trace the logic — don't trust comments or method names. Stubs and empty handlers are NOT "working."
2. **Completeness**: Is the full pipeline connected end-to-end? A UI wired to a never-emitted signal is not complete.
3. **Scope discipline**: Only touches the right files? No unnecessary abstractions or dead code?
4. **Convention compliance**: Qt/QSS sync, emulator accuracy (spec-sourced), test conventions (targeted), native C++ ownership and include discipline.
5. **Comment accuracy**: Comments must not make false claims. Flag and fix contradictions.

### Native-code review additions

- Are headers, implementation files, call sites, and tests updated together?
- Does QObject ownership remain valid and obvious?
- If threads are involved, is signal delivery mode explicit where needed?
- Did the change modify the smallest necessary build target instead of introducing parallel CMake behavior?

If ANY check fails, send specific feedback to Code Engineer. Do not report PASS until every check passes on actual code.

## Verification (MANDATORY before reporting back)

1. **Build**: Confirm `make build` passes.
2. **Test**: Delegate to Test Engineer for targeted tests. Failures → back to Code Engineer.
3. **Visual** (if UI changed): Delegate to Visual Engineer. Failures → back to Code Engineer.
4. **Knowledge update**: Update `.github/knowledge/` only when the work changes durable facts, debugging knowledge, workflow behavior, or architecture guidance that future agents should reuse.

Loop until all tasks pass. Do NOT report back with failures.

## Context Management During Iteration

- **Zero-return pull model**: All subagents write to session memory. Read-then-delete.
- **Dispatch with output path**: `ce-<task>-result.md`, `te-<task>-result.md`, `ve-<task>-result.md`
- **One checkpoint file**: `/memories/session/se-checkpoint.md`, overwritten each iteration.
- **After 2 failed iterations**: Overwrite checkpoint with: original task, attempted fixes (1 line each), test results, current hypothesis.
- **After 3 failed iterations**: Write the blocker to your result file and end.
- **Dispatch efficiently**: Pass Code Engineer the complete fix spec in one dispatch. Front-load ALL context.

## Result Formats

### Standard mode (to Project Lead):

```
# Result: <task>
## Status: PASS | FAIL
## Changed files
- path/to/file.cpp — one-line summary
## Verification
- Build: pass/fail
- Tests: pass/fail (which tests)
- Visual: pass/fail (if applicable)
## Blockers
- (if any)
```

### Debug mode (to Main Agent):

```
# Result: <bug>
## Status: FIXED | BLOCKED
## Root cause
One sentence.
## Changed files
- path/to/file.cpp — one-line summary
## Verification
- Build: pass/fail
- Tests: pass/fail (which tests)
## Key diagnostic
The error/signal that confirmed the fix.
## Blockers
- (if any)
```
