---
name: native-cpp-workflow
description: "Use for C++/Qt/CMake implementation or review work in AIO Server. Applies a large-repo native-code workflow: understand the affected subsystem, define acceptance criteria, make the smallest correct change, and verify with targeted build and tests."
user-invocable: false
---

# Native C++ Workflow

Use this skill for native code changes in `src/`, `include/`, `tests/`, and CMake files.

## Phase 1: Understand the target

- Read the affected code before planning a fix.
- Read the relevant knowledge note from `.github/knowledge/` and the matching scoped instructions under `.github/instructions/`.
- Determine what contract must remain stable: build graph, ownership, signal flow, emulator behavior, serialization, settings, or controller/navigation behavior.

## Phase 2: Define completion

- Restate the desired behavior in concrete terms.
- Name the likely files to touch before editing.
- Define focused acceptance criteria. Prefer testable statements over vague intent.
- Choose the smallest sufficient verification set: build only, targeted ctest, runtime repro, or visual confirmation.

## Phase 3: Implement

- Change the minimum number of files needed for a complete fix.
- Keep headers, implementation files, call sites, and tests aligned in one pass.
- Preserve local style instead of reformatting unrelated code.
- Avoid speculative abstractions and opportunistic cleanup.

## Phase 4: Review

Before calling work complete, check all of the following:

- Behavioral correctness: does the code actually implement the requested behavior?
- Completeness: are declarations, call sites, build files, and tests updated together?
- Ownership safety: are lifetimes, parents, and thread-delivery rules still correct?
- Scope discipline: did the change stay within the approved problem?
- Comment accuracy: do comments and labels still match the code?

## Phase 5: Verify

- Run `make build` for code changes unless explicitly told not to.
- Run the smallest relevant `ctest -R <pattern>` set for changed behavior.
- Use runtime debugging or visual verification only when code-level checks are not sufficient.
- Report residual risk explicitly if a subsystem cannot be fully exercised.

## Risk Tiers

- Critical: emulator timing, CPU/DMA/GPU correctness, persistence, remote-control protocol, startup/shutdown paths.
- High: Qt ownership, threaded signal delivery, WebEngine integration, shared navigation flows, build graph changes.
- Medium: page-level UI behavior, settings screens, focused component logic.
- Low: tests, docs, narrow refactors, isolated style-only adjustments.

Higher risk requires stricter verification and clearer acceptance criteria.
