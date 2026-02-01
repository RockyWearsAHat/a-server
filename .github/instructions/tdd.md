# Documentation-First TDD for Hardware Emulation

This document defines the TDD workflow for the AIO Entertainment System.

## Core Principle

**Tests mirror documentation, not implementation.**

```
TRUTH: GBATEK / Official Docs
  ↓
TESTS: Verify spec behavior
  ↓
CODE: Implement to pass tests
  ↓
MEMORY: Record what we learned
```

---

## The Cycle

### 1. Document First

Before touching code:

- Find the GBATEK section for the behavior
- Quote the exact specification
- Identify edge cases
- Update `docs/` if behavior isn't documented

### 2. Red: Failing Test

```cpp
/**
 * GBATEK: "[Exact quote]"
 */
TEST(Subsystem_Feature, Behavior_SpecRef) {
    // Arrange: Set up hardware state
    // Act: Perform operation
    // Assert: Verify bit-perfect result
}
```

Run test → Must fail for the right reason (missing implementation, NOT syntax error).

### 3. Green: Minimal Implementation

- Implement ONLY what spec requires
- Match GBATEK behavior bit-for-bit
- Add spec reference in code comments
- No optimizations beyond spec

### 4. Refactor: Clean & Document

- Remove duplication
- Improve names to match GBATEK terminology
- Add documentation comments
- Update `memory.md`
- Clean up orphan test files

### 5. Verify: Full Suite

```bash
make build && cd build/generated/cmake && ctest --output-on-failure
```

All tests must pass. No regressions.

---

## Test File Organization

### Official Tests (`tests/`)

| File                   | Coverage                      |
| ---------------------- | ----------------------------- |
| `CPUTests.cpp`         | ARM/Thumb instructions, flags |
| `PPUTests.cpp`         | PPU rendering, I/O registers  |
| `DMATests.cpp`         | DMA transfers, timing         |
| `EEPROMTests.cpp`      | Save protocol, DMA reads      |
| `MemoryMapTests.cpp`   | Memory regions, access rules  |
| `BIOSTests.cpp`        | BIOS function behavior        |
| `BootTest.cpp`         | Boot sequence scenarios       |
| `InputLogicTests.cpp`  | Keypad input                  |
| `ROMMetadataTests.cpp` | ROM header parsing            |

### Orphan Files (Workspace Root)

Files like `test_*.cpp`, `check_*.cpp`, `analyze_*.cpp` in the workspace root are **debugging artifacts**.

**Cleanup rules:**

- Fixed bug → Delete or archive
- Useful test → Move to `tests/`
- Analysis tool → Move to `scripts/`

---

## Code Documentation Standard

Every function implementing hardware behavior must reference the spec:

```cpp
/**
 * @brief [Brief description]
 *
 * GBATEK: "[Exact quote from specification]"
 * Source: https://problemkaputt.de/gbatek.htm#[section]
 *
 * @param [param] [description]
 * @return [description]
 */
```

---

## What Tests Should Verify

### Always Test

- Exact register values (bit-for-bit)
- Timing in cycles/scanlines
- Boundary conditions (0, max, overflow)
- Invalid input handling
- State transitions

### Never Test

- "It works in game X"
- Implementation details
- Performance characteristics (unless spec-mandated)

---

## Quick Reference

| Need to...          | Do this                                            |
| ------------------- | -------------------------------------------------- |
| Add feature         | Doc → Test → Implement → Verify                    |
| Fix bug             | Find spec → Test expected behavior → Fix → Verify  |
| Understand behavior | Check GBATEK first, then `docs/`, then `memory.md` |
| Clean up            | Delete orphan files after user confirms fix        |

---

## Anti-Patterns

❌ Writing implementation before tests
❌ Testing implementation details instead of spec behavior
❌ "Other emulators do X"
❌ Game-specific hacks
❌ Leaving orphan test files after fixes
