**Purpose**

- Keep the repo clean, fast, and focused on source code.

**Routine Cleanup**

- Logs: remove `debug.log` before new runs to avoid noise.
- Build outputs: never commit; if needed locally, clean with `rm -rf build/*`.
- Transient files: avoid committing `.sav`, `.state`, or ROMs.

**Recommended Commands (macOS/zsh)**

```sh
# Clean logs
rm -f debug.log

# Clean build outputs (safe: only artifacts)
rm -rf build/*

# Rebuild
make

# Run tests
./build/bin/CPUTests
./build/bin/EEPROMTests
```

**Hygiene Rules**

- All generated files live under `build/` or `build/generated/`.
- Never edit files under `build/`.
- CMake configuration only in `cmake/` and root `CMakeLists.txt`.
- Source code lives in `src/`, headers in `include/`, tests in `tests/`.
