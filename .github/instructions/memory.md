# Memory & Timing (AIO Instructions)

## Timing and Performance

### Interrupt Register Fast Path

ARM7TDMI::CheckInterrupts() uses GBAMemory::ReadInterruptRegs() to read IE/IF/IME directly from the io_regs[] array. This bypasses the full Read16() machinery (region dispatch, environment flag checks, tracer logic) and is critical for performance in tight CPU loops.

Why this is safe:
- IE/IF/IME are simple latched registers with no special read behavior
- No PPU flush is needed for interrupt status checks
- The register offsets (0x200, 0x202, 0x208) are compile-time constants
