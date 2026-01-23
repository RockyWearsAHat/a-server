# Code Style and Documentation Expectations

## Principles

- Prefer correctness and clarity over cleverness.
- Keep diffs small and localized.
- Avoid “emulator magic”: behavior should be explainable from docs/spec.

## Namespaces and structure

- All code lives under `AIO::*` namespaces.
- Do not use `using namespace`.
- Prefer forward declarations to reduce include cycles.

## Qt conventions

- Prefer signals/slots over direct callbacks for cross-thread communication.
- Signal names: past tense verb (e.g., `frameReady`, `romLoaded`).
- Slot names: imperative verb (e.g., `updateDisplay`, `handleInput`).
- Use `Q_OBJECT` macro in all QObject-derived classes.
- Keep GUI logic on the main thread; emulator work on a dedicated thread.
- Stylesheets live in `assets/qss/`; avoid inline style in C++.

## Comments

- Comments should explain _why_, not restate _what_ the code obviously does.
- When implementing hardware behavior, cite the source (GBATEK section or a project doc in `docs/`).

## Documentation updates

Any behavior change requires updating:

1. Relevant `docs/*` spec notes
2. Relevant `tests/*` coverage
3. `.github/instructions/memory.md` only if the high-level understanding/invariants changed

## Logging

- Prefer the project logger (`AIO::Emulator::Common::Logger`) so logs are captured.
- Avoid leaving `std::cout` instrumentation in mainline code.

## Testing discipline

- Tests are written before implementation.
- Tests mirror documentation/spec, not current behavior.
- Run the narrowest relevant tests first, then broaden.
