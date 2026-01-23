# Documentation-First TDD (Spec → Tests → Implementation)

This project’s correctness and maintainability depend on treating **documentation as the primary spec**, especially for emulation accuracy.

## The rule

1. **Write/Update documentation first** (what the system must do).
2. **Write tests that mirror the documentation** (not current behavior).
3. **Implement** until tests pass.
4. **Refactor** while keeping tests green.

If the documentation is incomplete, write the missing spec _before_ writing tests.

## Why this matters here

- Emulator correctness is subtle and regression-prone.
- “Trial and error” is allowed for discovery, but the **resulting truth must be codified** as:
  - a documented rule (docs)
  - a deterministic test (tests)
  - a clean implementation (src/include)

## Workflow checklist

### 1) Spec (docs)

- Identify the rule you’re implementing.
- Cite source material (GBATEK sections, known hardware behavior, or a project doc).
- Define:
  - inputs
  - outputs
  - timing/cycle constraints
  - edge cases

### 2) Tests (mirror docs)

- Add a test in `tests/*Tests.cpp` that encodes the spec.
- Prefer small, deterministic tests:
  - single instruction
  - single DMA transfer
  - one IO write and its observable effect
- Make tests independent: each test should set up the minimal state it needs.

**Important:** do not “lock in” current bugs. If the current behavior contradicts the spec, the test should assert the spec.

### 3) Implementation

- Implement the smallest change to satisfy the test.
- Keep changes local and avoid refactoring unrelated code.

### 4) Verification and cleanup

- Run targeted tests first, then expand:
  - run the specific test binary (e.g. `CPUTests`) if applicable
  - then CTest

- Only after the user confirms the problem is solved:
  - run `./scripts/clean.sh`
  - consider aggressive cleanup (`--all`) only if saves/dumps are no longer needed

## Trial-and-error policy

Trial and error is allowed for _discovering_ correct behavior, but it must end in:

- a written rule in `docs/`
- a test that enforces the rule
- code that reads clearly enough to be audited later

## Documentation templates

### Spec snippet template

- **Problem:**
- **Expected behavior:**
- **Hardware/source reference:**
- **Observable outputs:**
- **Edge cases:**
- **Test coverage:**

### Test naming

Use descriptive test names like:

- `DMA_Transfers_AdvanceTimingCorrectly`
- `EEPROM_ReadSequence_ProducesExpectedBits`
- `THUMB_ADCS_SetsCarryAndOverflow`
