---
description: "Runtime debugging workflow — crash diagnosis, debug.log, signal interpretation."
applyTo: "src/**/*.cpp,include/**/*.h,include/**/*.hpp"
---

# Runtime Debugging

## Launch reference

| Purpose          | Command                                                              |
| ---------------- | -------------------------------------------------------------------- |
| Normal launch    | `./build/bin/AIOServer`                                              |
| Debug launch     | `AIO_LOG_LEVEL=debug ./build/bin/AIOServer`                          |
| Non-focus launch | `./build/bin/AIOServer --no-activate`                                |
| Visual capture   | `python3 scripts/visual_dev_loop.py snapshot`                        |
| Build            | `make build`                                                         |
| Targeted test    | `cd build/generated/cmake && ctest -R <pattern> --output-on-failure` |

## Known gotchas

- `debug.log` is written to the project root (working directory), not next to the binary.
- Qt parent-child ownership: deleting a parent deletes all children. Double-free crashes often come from manually deleting a widget that Qt already owns.
- Qt signal/slot across threads requires `Qt::QueuedConnection` or will crash silently or non-deterministically.
- QSettings persists state between runs. Stale settings can cause unexpected behavior after refactors — clear with `QSettings().clear()` if state-related bugs don't reproduce cleanly.
- Qt WebEngine runs in a separate process. Crashes in WebEngine content don't produce SIGSEGV in the main process — check WebEngine's own console output.

## Crash signals

| Exit code | Signal  | Meaning                           | First step                                        |
| --------- | ------- | --------------------------------- | ------------------------------------------------- |
| 139       | SIGSEGV | Segmentation fault (null/bad ptr) | Check last log line in debug.log, find the deref  |
| 134       | SIGABRT | Assertion / abort()               | Read the assertion message in stderr or debug.log |
| 136       | SIGFPE  | Division by zero / FP error       | Find the arithmetic operation in the call path    |
| 137       | SIGKILL | Killed (OOM or external)          | Check memory usage, large allocations             |
| 0         | —       | Clean exit                        | Not a crash — check logic for early exit          |

## Reading debug.log

- `debug.log` is written to the working directory (project root).
- Filter for relevant subsystem: `grep -i "airplay\|youtube\|gpu\|crash\|error\|fatal" debug.log`
- The last few lines before a crash are the most important — they show what was executing when it died.
- Look for patterns: repeated errors, null pointer warnings, failed assertions, resource exhaustion.

## Diagnosis workflow

1. **Reproduce**: Run the app and confirm the crash. Note the exit code.
2. **Read the log**: Check `debug.log` for the last lines before the crash. This tells you which subsystem was active.
3. **Locate the code**: Use the source map (`.github/knowledge/source-map.md`) to find the files for that subsystem.
4. **Form a hypothesis**: Based on the log output and crash signal, identify the likely code path. Common causes:
   - SIGSEGV: null pointer dereference, use-after-free, uninitialized pointer, bad cast
   - SIGABRT: failed Q_ASSERT, std::abort, uncaught exception
   - Logic errors: wrong state machine transition, missing initialization, race condition
5. **Fix and verify**: Make the fix, rebuild (`make build`), relaunch, confirm the crash is gone.
6. **Run targeted tests**: Use `ctest -R <pattern>` for the affected subsystem to ensure no regressions.

## Common runtime failure patterns

- **Crash on launch**: Usually a constructor or initialization issue. Check `main.cpp`, `MainWindow` setup, and any services started at boot (AirPlay, RemoteControl, etc.).
- **Crash on page navigation**: Check the page's constructor and any signals/slots wired during page creation. Null widget pointers are common.
- **Crash in emulator**: Check the emulation loop, memory access bounds, and unimplemented opcodes.
- **WebEngine crashes**: Qt WebEngine has its own process model. Check for missing initialization, invalid URLs, or resource exhaustion.

## When unit tests aren't enough

Unit tests verify isolated logic. Runtime crashes often involve:

- Object lifecycle (Qt parent-child ownership, deletion order)
- Signal/slot connections across threads
- Resource initialization order at startup
- WebEngine process model interactions

For these, the diagnosis workflow above (log → locate → hypothesize → fix → relaunch) is the primary verification path. Write a targeted unit test after the fix to prevent regression where possible.
