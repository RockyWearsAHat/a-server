---
description: "Core C++ implementation conventions for native AIO Server code."
applyTo: "src/**/*.cpp,include/**/*.h,include/**/*.hpp,tests/**/*Tests.cpp"
---

# Native C++ Rules

- Follow the surrounding file's structure and naming before introducing a new pattern.
- Prefer the smallest complete fix. If a header change affects call sites or tests, update them in the same pass.
- Keep implementation in `.cpp` files unless the file is already intentionally header-only.
- Add only the includes you need. Preserve the file's local include ordering pattern instead of reformatting unrelated blocks.
- Use forward declarations in headers when they reduce coupling without fighting existing style.
- Prefer references for required non-null collaborators and pointers where nullability, optional ownership, or QObject tree ownership are part of the contract.
- Keep const-correctness aligned with actual semantics. Mark methods and parameters `const` when that improves correctness and matches surrounding code.
- Avoid speculative abstractions. A new helper, class, or template should exist because the current change needs it now.
- For non-obvious logic, comment the invariant or hardware rule, not a line-by-line narration.
- When behavior changes, update or add focused tests that prove the changed behavior.

## Verification Baseline

- Build the affected native code with `make build` unless the task is explicitly research-only.
- Use targeted `ctest -R <pattern>` coverage instead of broad test runs.
- If a runtime-only bug is involved, pair code-level checks with the runtime debugging workflow in `.github/instructions/runtime-debugging.instructions.md`.
