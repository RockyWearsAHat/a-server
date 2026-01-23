# Workspace Hygiene (Artifacts, Cleanup, and When)

This repo generates a lot of _regeneratable_ build/test/debug output. Keeping the workspace clean improves signal-to-noise and reduces accidental commits.

## What is an artifact?

Artifacts are files and folders that are not source-of-truth and can be regenerated.

Common examples:

- Build outputs: `build/bin`, `build/lib`, `build/generated`
- Logs: `debug.log`, `*.log`
- Editor backups: `*.bak`, `*.swp`, `*.tmp`, `*.orig`
- Diagnostics: `dumps*` folders, `*.ppm` frame dumps

## What is _not_ automatically deleted?

These may be needed for reproduction or comparison:

- Emulator runtime saves: `*.sav`, `*.state`, `*.srm`
- Diagnostic dumps: `dumps*`, `*.ppm`

They are only removed by explicit request.

## Cleanup policy

### Routine cleanup (safe-by-default)

Run:

- `./scripts/clean.sh`

This removes logs, common backup files, and build outputs. It keeps saves and dumps.

### Aggressive cleanup (only after verification)

Only after the user confirms that the issue is solved, you may remove larger artifacts:

- `./scripts/clean.sh --all`

Or selectively:

- `./scripts/clean.sh --saves` (or `AIO_CLEAN_SAVES=1`)
- `./scripts/clean.sh --dumps` (or `AIO_CLEAN_DUMPS=1`)

## Required rule: user verification gate

If a task is “fix bug X”, the cleanup step is **blocked** until the user says the behavior is solved.

Reason: saves/dumps are often required to reproduce and verify the fix.

## Git hygiene

- `build/`, ROMs, saves, dumps, logs are ignored by `.gitignore`.
- Do not add or commit artifacts.
