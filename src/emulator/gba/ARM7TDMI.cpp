#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <emulator/common/Logger.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <iostream>
#include <mutex>
#include <string>

namespace AIO::Emulator::GBA {
using AIO::Emulator::Common::Logger;
using AIO::Emulator::Common::LogLevel;

// Branch logging - used for debugging invalid jumps and crash logs
static std::deque<std::pair<uint32_t, uint32_t>> branchLog;

// Crash notification callback (set by GUI)
void (*CrashPopupCallback)(const char *logPath) = nullptr;

static GBAMemory *g_memoryForLog = nullptr;
static bool g_thumbModeForLog = false;

namespace {
// NOTE: These traces were added for deep bring-up debugging (IRQ/LR/SMA2).
// They are extremely noisy and can drastically slow wall-clock-limited runs.
// Keep them compiled but disabled by default.
constexpr bool kEnableHeavyCpuTraces = false;

} // namespace

static void LogBranch([[maybe_unused]] uint32_t from,
                      [[maybe_unused]] uint32_t to) {
  branchLog.push_back({from, to});
  if (branchLog.size() > 50)
    branchLog.pop_front();

  // Treat jumps into the middle of the instruction encoding space as invalid.
  if ((to & 0xFF000000) == 0xE3000000) {
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "PC CORRUPTION (looks like opcode): 0x%08x -> 0x%08x", from, to);
  }
}

// LCOV_EXCL_START - Debug trace functions only enabled via
// kEnableHeavyCpuTraces or env vars
static void TracePCWrite(const char *source, uint32_t pcFrom, uint32_t pcTo,
                         uint32_t instruction, uint32_t extra0 = 0,
                         uint32_t extra1 = 0) {
  // Keep this extremely focused; only print when the destination looks wrong.
  const bool looksLikeOpcode = ((pcTo & 0xFF000000) == 0xE3000000) ||
                               ((pcTo & 0xFF000000) == 0xE2000000);
  const bool looksInvalidAddr =
      !((pcTo < 0x00004000) ||
        // EWRAM is 256KB mirrored across the whole 0x02xxxxxx region.
        ((pcTo & 0xFF000000u) == 0x02000000u) ||
        // IWRAM is 32KB mirrored across the whole 0x03xxxxxx region.
        ((pcTo & 0xFF000000u) == 0x03000000u) ||
        // VRAM is mirrored within 0x06xxxxxx.
        ((pcTo & 0xFF000000u) == 0x06000000u) ||
        // GamePak ROM is mirrored in 0x08..0x0D.
        (pcTo >= 0x08000000u && pcTo < 0x0E000000u));
  if (looksLikeOpcode || looksInvalidAddr) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "PC WRITE [%s] from=0x%08x to=0x%08x "
                              "instr=0x%08x extra0=0x%08x extra1=0x%08x",
                              source, pcFrom, pcTo, instruction, extra0,
                              extra1);
  }
}

// These traces rely on ARM7TDMI::Step() capturing a per-instruction context.
// Keep them as free functions, but allow Step() / decode to pass in the
// authoritative context.
static void TraceR8Write(uint32_t instrAddr, bool thumb, uint32_t instruction,
                         uint32_t oldVal, uint32_t newVal) {
  if (!kEnableHeavyCpuTraces)
    return;
  if (oldVal == newVal)
    return;
  // Always log when R8 is written to 0, regardless of old value, so we can
  // pinpoint the exact instruction that zeros the block-transfer base.
  if (newVal == 0 || (instrAddr >= 0x08001000 && instrAddr <= 0x08002000)) {
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "R8 WRITE fromPC=0x%08x mode=%c instr=0x%08x old=0x%08x new=0x%08x",
        instrAddr, thumb ? 'T' : 'A', instruction, oldVal, newVal);
  }
}

static void TraceR11Write(uint32_t instrAddr, bool thumb, uint32_t instruction,
                          uint32_t oldVal, uint32_t newVal) {
  if (!kEnableHeavyCpuTraces)
    return;
  if (oldVal == newVal)
    return;
  if (newVal == 0 || (instrAddr >= 0x08001000 && instrAddr <= 0x08002000)) {
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "R11 WRITE fromPC=0x%08x mode=%c instr=0x%08x old=0x%08x new=0x%08x",
        instrAddr, thumb ? 'T' : 'A', instruction, oldVal, newVal);
  }
}

// LCOV_EXCL_STOP

static void TraceWatchWrite32(uint32_t instrAddr, bool thumb,
                              uint32_t instruction, uint32_t addr,
                              uint32_t value, uint32_t sp) {
  // OGDK buffer tracing disabled for performance
  // (was tracking 0x03007B14, 0x03007B18, 0x03007B1C)

  if (!kEnableHeavyCpuTraces)
    return;
  // Watch the exact stack slots later popped into r3/r4 before MOV r8,r3.
  // From logs: POP {r3,r4} at 0x08007320 uses SP=0x03007D7C.
  const bool watchSma2PopSlots = (addr == 0x03007D7C || addr == 0x03007D80);

  // Watch the Thumb epilogue that does:
  //   0x080014DE: POP {r4}
  //   0x080014E0: POP {r0}
  //   0x080014E2: BX r0
  // If [SP+4] is 0, we end up branching to 0x00000000.
  const bool watchThumbReturnSlots = (addr == 0x03007DDC || addr == 0x03007DE0);

  // Watch the BIOS IRQ trampoline stack frame (push/pop of r0-r3,r12,lr) at the
  // top of IWRAM. This is where LR becomes 0 in the failing path, leading to
  // SUBS PC,LR,#4 => 0xFFFFFFFC.
  const bool watchIrqFrame = (addr >= 0x03007F68 && addr <= 0x03007F7C);

  if (watchSma2PopSlots || watchThumbReturnSlots || watchIrqFrame) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "WATCH32 WRITE at PC=0x%08x mode=%c instr=0x%08x "
                              "addr=0x%08x val=0x%08x SP=0x%08x",
                              instrAddr, thumb ? 'T' : 'A', instruction, addr,
                              value, sp);
  }
}

static void TraceWatchRead32(uint32_t instrAddr, bool thumb,
                             uint32_t instruction, uint32_t addr,
                             uint32_t value, uint32_t sp) {
  if (!kEnableHeavyCpuTraces)
    return;
  const bool watchIrqFrame = (addr >= 0x03007F68 && addr <= 0x03007F9C);
  const bool watchThumbReturnSlots = (addr == 0x03007DDC || addr == 0x03007DE0);

  if (watchIrqFrame || watchThumbReturnSlots) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "WATCH32 READ  at PC=0x%08x mode=%c instr=0x%08x "
                              "addr=0x%08x val=0x%08x SP=0x%08x",
                              instrAddr, thumb ? 'T' : 'A', instruction, addr,
                              value, sp);
  }
}
// LCOV_EXCL_STOP

ARM7TDMI::ARM7TDMI(GBAMemory &mem) : memory(mem) { Reset(); }

ARM7TDMI::~ARM7TDMI() = default;

void ARM7TDMI::Reset() {
  std::memset(registers, 0, sizeof(registers));
  spsr = 0;
  thumbMode = false;
  halted = false;

  // Initialize Banked Registers (stack tops chosen to match common GBA usage)
  r13_svc = 0x03007FE0;
  r14_svc = 0;
  spsr_svc = 0;
  r13_irq = 0x03007FA0;
  r14_irq = 0;
  spsr_irq = 0;
  r13_usr = 0x03007F00;
  r14_usr = 0;

  // When no real BIOS is present, use DirectBoot: skip BIOS and jump straight
  // to ROM entry (0x08000000). Hardware state is initialized by
  // GBAMemory::Reset() to match what BIOS would set up.
  //
  // When a user-provided BIOS image is loaded (LLE BIOS), start execution
  // from 0x00000000 in Supervisor mode and let the real BIOS perform all
  // initialization.
  const bool hasLLEBios = memory.HasLLEBIOS();
  if (!hasLLEBios) {
    cpsr = CPUMode::SYSTEM; // System Mode
    registers[Register::PC] = 0x08000000;
    registers[Register::SP] = r13_usr; // Initialize SP (User/System)
  } else {
    cpsr = CPUMode::SUPERVISOR; // BIOS starts in SVC mode
    registers[Register::PC] = 0x00000000;
    registers[Register::SP] = r13_svc; // Use SVC stack for BIOS
  }

  // Initialize pipeline - will be filled on first Step()
  FlushPipeline();
}

void ARM7TDMI::FlushPipeline() {
  // Invalidate prefetch buffer - called when PC changes non-sequentially
  // (branches, interrupts, exceptions). On real ARM7TDMI, a branch causes
  // the pipeline to flush and refetch from the new PC.
  prefetchValid[0] = false;
  prefetchValid[1] = false;
  prefetch[0] = 0;
  prefetch[1] = 0;
  prefetchAddr[0] = 0;
  prefetchAddr[1] = 0;
  prefetchThumb[0] = false;
  prefetchThumb[1] = false;
}

void ARM7TDMI::RefillPipeline() {
  // Fill both prefetch slots from current PC.
  // Called after FlushPipeline() or when pipeline is empty.
  // This simulates the ARM7TDMI refetching instructions after a branch.
  const uint32_t pc = registers[15];
  const bool thumb = IsThumbMode(cpsr);

  if (thumb) {
    // Thumb: 16-bit instructions, halfword aligned
    const uint32_t addr0 = pc & ~1u;
    const uint32_t addr1 = addr0 + 2u;
    prefetch[0] = memory.ReadInstruction16(addr0);
    prefetch[1] = memory.ReadInstruction16(addr1);
    prefetchAddr[0] = addr0;
    prefetchAddr[1] = addr1;
    prefetchThumb[0] = true;
    prefetchThumb[1] = true;
  } else {
    // ARM: 32-bit instructions, word aligned
    const uint32_t addr0 = pc & ~3u;
    const uint32_t addr1 = addr0 + 4u;
    prefetch[0] = memory.ReadInstruction32(addr0);
    prefetch[1] = memory.ReadInstruction32(addr1);
    prefetchAddr[0] = addr0;
    prefetchAddr[1] = addr1;
    prefetchThumb[0] = false;
    prefetchThumb[1] = false;
  }
  prefetchValid[0] = true;
  prefetchValid[1] = true;
}

void ARM7TDMI::SwitchMode(uint32_t newMode) {
  uint32_t oldMode = GetCPUMode(cpsr);
  if (oldMode == newMode)
    return;

  // Save current registers to bank
  switch (oldMode) {
  case CPUMode::USER:
  case CPUMode::SYSTEM: // User/System
    r13_usr = registers[Register::SP];
    r14_usr = registers[Register::LR];
    break;
  case CPUMode::IRQ:
    r13_irq = registers[Register::SP];
    r14_irq = registers[Register::LR];
    spsr_irq = spsr;

    if (kEnableHeavyCpuTraces) {
      // Debug: Track IRQ stack changes with detailed context
      static uint32_t last_r13_irq = 0x03007FA0;
      if (r13_irq != last_r13_irq) {
        std::cerr << "[MODE SWITCH SAVE] IRQ SP changed: 0x" << std::hex
                  << last_r13_irq << " -> 0x" << r13_irq
                  << " (from registers[13]=0x" << registers[Register::SP]
                  << ") PC=0x" << registers[15] << std::dec << std::endl;
        last_r13_irq = r13_irq;
      }
    }
    break;
  case CPUMode::SUPERVISOR:
    r13_svc = registers[Register::SP];
    r14_svc = registers[Register::LR];
    spsr_svc = spsr;
    break;
  }

  // Load new registers from bank
  switch (newMode) {
  case CPUMode::USER:
  case CPUMode::SYSTEM: // User/System
    registers[Register::SP] = r13_usr;
    registers[Register::LR] = r14_usr;
    // System/User modes do not have an SPSR.
    spsr = 0;
    break;
  case CPUMode::IRQ:
    if (kEnableHeavyCpuTraces) {
      std::cerr << "[MODE SWITCH LOAD] Loading IRQ mode: r13_irq=0x" << std::hex
                << r13_irq << " r14_irq=0x" << r14_irq << " PC=0x"
                << registers[15] << std::dec << std::endl;
    }
    registers[Register::SP] = r13_irq;
    if (kEnableHeavyCpuTraces) {
      std::cerr << "[MODE SWITCH LOAD] After assignment: registers[13]=0x"
                << std::hex << registers[Register::SP] << std::dec << std::endl;
    }
    registers[Register::LR] = r14_irq;
    spsr = spsr_irq;

    // Safety check: IRQ stack should never be 0
    if (registers[Register::SP] == 0) {
      if (kEnableHeavyCpuTraces) {
        std::cerr << "[FATAL] IRQ stack corrupted! r13_irq=0x" << std::hex
                  << r13_irq << " Re-initializing to 0x03007FA0 PC=0x"
                  << registers[15] << std::dec << std::endl;
      }
      r13_irq = 0x03007FA0;
      registers[Register::SP] = r13_irq;
    }

    if (kEnableHeavyCpuTraces) {
      // LCOV_EXCL_START – debug trace (throttled IRQ mode entry log)
      // Debug: Log IRQ mode entry
      static int irq_entry_count = 0;
      if (irq_entry_count++ < 10) {
        std::cerr << "[MODE SWITCH] Entering IRQ mode, SP=0x" << std::hex
                  << registers[Register::SP] << " PC=0x" << registers[15]
                  << std::dec << std::endl;
      }
      // LCOV_EXCL_STOP
    }
    break;
  case CPUMode::SUPERVISOR:
    registers[Register::SP] = r13_svc;
    registers[Register::LR] = r14_svc;
    spsr = spsr_svc;
    break;
  }

  SetCPUMode(cpsr, newMode);
}

void ARM7TDMI::CheckInterrupts() {
  // Log if we're about to check interrupts during BIOS execution
  if (kEnableHeavyCpuTraces && registers[15] >= 0x180 &&
      registers[15] < 0x1d0) {
    uint16_t ie = memory.Read16(IORegs::REG_IE);
    uint16_t if_reg = memory.Read16(IORegs::REG_IF);
    uint16_t ime = memory.Read16(IORegs::REG_IME);
    std::cerr << "[CheckInterrupts during BIOS] PC=0x" << std::hex
              << registers[15] << " IE=0x" << ie << " IF=0x" << if_reg
              << " IME=0x" << ime << " IRQDisabled=" << IRQDisabled(cpsr)
              << std::dec << std::endl;
  }

  uint16_t ime = memory.Read16(IORegs::REG_IME);
  uint16_t ie = memory.Read16(IORegs::REG_IE);
  uint16_t if_reg = memory.Read16(IORegs::REG_IF);

  // Wake from HALT/STOP/IntrWait if any enabled interrupt is pending.
  // Do NOT auto-resume debugger breakpoints/stepback halts.
  if (halted && sleepHalt && (ie & if_reg)) {
    halted = false;
    sleepHalt = false;
  }

  if (!(ime & 1)) {
    return;
  }
  if (IRQDisabled(cpsr)) {
    return;
  }

  if (ie & if_reg) {
    // Calculate triggered interrupts NOW before PPU/Timers run
    // On real GBA, BIOS reads IE & IF atomically at IRQ entry
    uint16_t triggered = ie & if_reg;

    // NOTE: The real GBA BIOS IRQ handler (0x128-0x13C) does NOT write
    // BIOS_IF or any scratch locations. The game's own handler is responsible
    // for setting BIOS_IF (0x03007FF8) after acknowledging interrupts.

    // Capture current execution state before switching modes.
    // Prefer CPSR.T but tolerate occasional thumbMode/CPSR divergence during
    // debugging.
    const bool cpsrThumb = IsThumbMode(cpsr);
    const bool wasThumb = thumbMode || cpsrThumb;

    // Save CPSR to SPSR_irq (restore target state on SUBS/MOVS PC return).
    uint32_t oldCpsr = cpsr;
    SetCPSRFlag(oldCpsr, CPSR::FLAG_T, wasThumb);

    // LCOV_EXCL_START – kEnableHeavyCpuTraces debug log
    if (kEnableHeavyCpuTraces) {
      static int irqEntryLogCount = 0;
      if (irqEntryLogCount++ < 20) {
        std::cerr << "[IRQ ENTRY] PC=0x" << std::hex << registers[15]
                  << " thumbMode=" << (thumbMode ? 1 : 0) << " CPSR=0x" << cpsr
                  << " cpsrThumb=" << (cpsrThumb ? 1 : 0)
                  << " wasThumb=" << (wasThumb ? 1 : 0) << " savedSPSR=0x"
                  << oldCpsr << std::dec << std::endl;
      }
    }
    // LCOV_EXCL_STOP

    // Switch to IRQ Mode
    SwitchMode(CPUMode::IRQ);
    spsr = oldCpsr;
    spsr_irq = oldCpsr; // Also save to banked SPSR for System Mode return

    // Disable Thumb, enable IRQ mask
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
    SetCPSRFlag(cpsr, CPSR::FLAG_I, true);

    // IRQ exception return address (LR_irq): ARM7TDMI uses an offset such that
    // the BIOS trampoline can return with `SUBS PC, LR, #4` regardless of
    // whether the interrupted code was ARM or Thumb.
    // For IRQ, LR_irq should be (address of the next instruction) + 4.
    r14_irq = registers[15] + 4;
    registers[Register::LR] = r14_irq;
    if (kEnableHeavyCpuTraces) {
      std::cerr << "[IRQ SETUP] After LR assignment: LR=0x" << std::hex
                << registers[Register::LR] << " SP=0x"
                << registers[Register::SP] << std::dec << std::endl;
    }

    // Jump to BIOS IRQ Trampoline at ExceptionVector::IRQ
    registers[Register::PC] = ExceptionVector::IRQ;
    FlushPipeline(); // IRQ entry invalidates prefetched instructions
    if (kEnableHeavyCpuTraces) {
      std::cerr << "[IRQ SETUP] After PC assignment: PC=0x" << std::hex
                << registers[Register::PC] << " SP=0x"
                << registers[Register::SP] << std::dec << std::endl;
    }
  }
}

// Public wrapper to allow emulation core to service IRQs immediately after
// peripherals (PPU/Timers/APU) update, reducing latency in tight polling loops.
void ARM7TDMI::PollInterrupts() { CheckInterrupts(); }

void ARM7TDMI::Step() {
  // Reset per-instruction HLE cycle accumulator.
  // BIOS SWIs may advance peripheral time in bulk; we expose that to the outer
  // loop via ConsumeHLECycles().
  hleCyclesThisStep = 0;

  // Keep the boolean decode mode in sync with CPSR.T.
  // A Thumb/ARM divergence here can cause us to decode Thumb bytes as ARM and
  // trigger cascading state corruption.
  thumbMode = IsThumbMode(cpsr);

  // Capture instruction context once per Step() so traces can't accidentally
  // attribute a register write to the wrong opcode.
  currentInstrAddr = registers[15];
  currentInstrThumb = thumbMode;
  currentOp16 = 0;
  currentOp32 = 0;

  // Record CPU state snapshot only when debugger features are active
  // (breakpoints or single-step)
  static const size_t kMaxHistory = 1024;
  const bool recordHistory = singleStep || !breakpoints.empty();
  if (recordHistory) {
    cpuHistory.push_back({});
    auto &s = cpuHistory.back();
    for (int i = 0; i < 16; ++i)
      s.registers[i] = registers[i];
    s.cpsr = cpsr;
    s.spsr = spsr;
    s.thumbMode = thumbMode;
    if (cpuHistory.size() > kMaxHistory)
      cpuHistory.erase(cpuHistory.begin());
  }
  static uint32_t lastPC = 0;

  // Breakpoint / single-step handling
  uint32_t bpPC = registers[15];
  for (auto addr : breakpoints) {
    if (bpPC == addr) {
      halted = true;
      debuggerHalt = true;
      sleepHalt = false;
      std::cout << "[BREAKPOINT] Hit at PC=0x" << std::hex << bpPC << std::dec
                << std::endl;
      DumpState(std::cout);
      return;
    }
  }
  if (singleStep) {
    // Execute exactly one instruction then halt
    singleStep = false;
    // fall-through to instruction execution
  }

  // Sanity Check SP - crash with detailed diagnostics
  if (registers[13] == 0) {
    std::cerr << "\n[FATAL] SP is 0! CPSR=0x" << std::hex << cpsr << " Mode=0x"
              << (cpsr & 0x1F) << std::dec << std::endl;
    std::cerr << "Last PC: 0x" << std::hex << lastPC << std::dec << std::endl;
    std::cerr << "Current PC: 0x" << std::hex << registers[15] << std::dec
              << std::endl;
    std::cerr << "Thumb mode: " << (thumbMode ? "YES" : "NO") << std::endl;
    std::cerr << "r13_irq: 0x" << std::hex << r13_irq << std::dec << std::endl;
    std::cerr << "r13_usr: 0x" << std::hex << r13_usr << std::dec << std::endl;

    // Show the instruction that's about to execute
    if (registers[15] < 0x4000) {
      uint32_t opcode = memory.Read32(registers[15]);
      std::cerr << "Next instruction at PC: 0x" << std::hex << opcode
                << std::dec << std::endl;
    }

    // Dump memory around IRQ stack to see if it's been overwritten
    std::cerr << "\nMemory at IRQ stack base (0x03007FA0):" << std::endl;
    for (uint32_t addr = 0x03007FA0; addr < 0x03007FC0; addr += 4) {
      uint32_t val = memory.Read32(addr);
      std::cerr << "  0x" << std::hex << addr << ": 0x" << val << std::dec
                << std::endl;
    }

    std::cerr
        << "\n** SP CORRUPTION DETECTED - IRQ stack has been overwritten. **\n"
        << std::endl;

    // Write crash info to a log file (same pattern as invalid-PC handler).
    halted = true;
    FILE *logFile = fopen("crash_log.txt", "a");
    if (logFile) {
      fprintf(logFile, "==== Emulator Crash (SP==0) ====\n");
      fprintf(logFile, "LastPC: 0x%08X\n", lastPC);
      fprintf(logFile, "PC:     0x%08X\n", registers[15]);
      fprintf(logFile, "SP:     0x%08X\n", registers[13]);
      fprintf(logFile, "LR:     0x%08X\n", registers[14]);
      fprintf(logFile, "CPSR:   0x%08X\n", cpsr);
      fprintf(logFile, "Mode:   0x%02X\n", (unsigned)(cpsr & 0x1F));
      fprintf(logFile, "Thumb:  %d\n", thumbMode ? 1 : 0);
      fprintf(logFile, "r13_irq: 0x%08X\n", r13_irq);
      fprintf(logFile, "r13_usr: 0x%08X\n", r13_usr);
      for (int i = 0; i < 16; ++i) {
        fprintf(logFile, "R%d: 0x%08X\n", i, registers[i]);
      }
      fprintf(logFile, "IRQ stack base dump (0x03007FA0..0x03007FBC):\n");
      for (uint32_t addr = 0x03007FA0; addr < 0x03007FC0; addr += 4) {
        fprintf(logFile, "  0x%08X: 0x%08X\n", addr, memory.Read32(addr));
      }
      fclose(logFile);
    }

    if (CrashPopupCallback)
      CrashPopupCallback("crash_log.txt");
    return;
  }

  // Log BIOS IRQ return path (0x1c0-0x1d0) with LR value
  if (kEnableHeavyCpuTraces && registers[15] >= 0x1c0 &&
      registers[15] <= 0x1d0) {
    std::cerr << "[BIOS IRQ RETURN] PC=0x" << std::hex << registers[15]
              << " SP=0x" << registers[13] << " LR_irq=0x" << r14_irq
              << " Mode=0x" << (cpsr & 0x1F) << std::dec << std::endl;
  }

  // Log ALL BIOS trampoline execution (0x180-0x1d0)
  if (kEnableHeavyCpuTraces && registers[15] >= 0x180 &&
      registers[15] < 0x1d0) {
    uint32_t nextInstr = memory.ReadInstruction32(registers[15]);
    std::cerr << "[BIOS TRAMPOLINE] PC=0x" << std::hex << registers[15]
              << " Instr=0x" << nextInstr << " SP=0x" << registers[13]
              << " R3=0x" << registers[3] << " R7=0x" << registers[7]
              << " LR=0x" << registers[14] << " Mode=0x" << (cpsr & 0x1F)
              << std::dec << std::endl;

    // At 0x1b4, we load the user IRQ handler from [R3+4]
    if (registers[15] == 0x1b4) {
      uint32_t handlerAddr = memory.Read32(registers[3] + 4);
      std::cerr << "[BIOS] Loading IRQ handler from 0x" << std::hex
                << (registers[3] + 4) << " -> 0x" << handlerAddr << std::dec
                << std::endl;
    }
  }

  // Log jumps near the crash location (0x809e390-0x809e3a0)
  if (kEnableHeavyCpuTraces && registers[15] >= 0x809e390 &&
      registers[15] <= 0x809e3a0) {
    uint32_t instr = memory.ReadInstruction32(registers[15]);
    std::cerr << "[NEAR CRASH LOCATION] PC=0x" << std::hex << registers[15]
              << " SP=0x" << registers[13] << " LR_irq=0x" << r14_irq
              << " Mode=0x" << (cpsr & 0x1F) << " Instruction=0x" << instr
              << std::dec << std::endl;
  }

  CheckInterrupts();

  if (halted) {
    // CPU stopped — peripheral advancement handled by GBA::Step()
    return;
  }

  // Validate PC region.
  // IMPORTANT: Do not canonicalize PC into base ranges. On hardware, EWRAM,
  // IWRAM, VRAM, and GamePak regions are mirrored by address line decoding;
  // the CPU's PC retains the full mirrored address and games may use that
  // value for PC-relative calculations.
  uint32_t pc = registers[15];
  bool valid = false;
  if (pc < 0x00004000u)
    valid = true; // BIOS
  if ((pc & 0xFF000000u) == 0x02000000u)
    valid = true; // EWRAM mirrors (0x02000000-0x02FFFFFF)
  if ((pc & 0xFF000000u) == 0x03000000u)
    valid = true; // IWRAM mirrors (0x03000000-0x03FFFFFF)
  if ((pc & 0xFF000000u) == 0x06000000u)
    valid = true; // VRAM mirrors (0x06000000-0x06FFFFFF)
  if (pc >= 0x08000000u && pc < 0x0E000000u)
    valid = true; // GamePak ROM mirrors (0x08000000-0x0DFFFFFF)
  if (!valid) {
    std::cerr << "[FATAL] Invalid PC: 0x" << std::hex << pc << std::dec
              << std::endl;
    halted = true;
    sleepHalt = false;
    debuggerHalt = false;
    // Write crash info to log file asynchronously
    FILE *logFile = fopen("crash_log.txt", "a");
    if (logFile) {
      fprintf(logFile, "==== Emulator Crash ====");
      fprintf(logFile, "\nPC: 0x%08X\n", pc);
      for (int i = 0; i < 16; ++i)
        fprintf(logFile, "R%d: 0x%08X\n", i, registers[i]);
      fprintf(logFile, "CPSR: 0x%08X\n", cpsr);
      fprintf(logFile, "ThumbMode: %d\n", thumbMode);
      fprintf(logFile, "BranchLog (last 50):\n");
      for (const auto &br : branchLog)
        fprintf(logFile, "  0x%08X -> 0x%08X\n", br.first, br.second);
      // Optionally dump a small memory region around SP
      uint32_t sp = registers[13];
      fprintf(logFile, "Stack Dump:\n");
      for (int i = 0; i < 64; i += 4)
        fprintf(logFile, "  0x%08X: 0x%08X\n", sp + i, memory.Read32(sp + i));
      fclose(logFile);
    }
    // Signal GUI to show crash popup and allow log viewing if callback is set
    if (CrashPopupCallback)
      CrashPopupCallback("crash_log.txt");
    return;
  }

  // Keep a local copy for the rest of this step.
  // (Do not modify registers[15] here.)
  pc = registers[15];

  // Instruction fetch on ARM7TDMI ignores the low address bits:
  // - ARM state fetches are word-aligned (bits[1:0]=0)
  // - Thumb state fetches are halfword-aligned (bit0=0)
  // If something wrote an unaligned PC, align it here so we fetch the same
  // opcodes real hardware would.
  const uint32_t pcAligned = thumbMode ? (pc & ~1u) : (pc & ~3u);
  if (pcAligned != pc) {
    registers[15] = pcAligned;
    pc = pcAligned;
  }

  lastPC = pc;

  // BIOS handling:
  // We do not ship a full BIOS ROM, but we *do* install a real
  // instruction-level IRQ vector + trampoline in the BIOS region (see
  // GBAMemory::InitializeHLEBIOS/Reset). For correctness, let that trampoline
  // execute as normal instructions. For other BIOS entry points that games may
  // call directly, we still provide HLE.
  if (!memory.HasLLEBIOS() && pc < 0x4000) {
    const uint32_t pcAligned = thumbMode ? (pc & ~1u) : (pc & ~3u);

    const bool inIrqVector = (pcAligned == 0x00000018u);
    // The IRQ trampoline is installed in BIOS space; execute it as
    // normal ARM instructions rather than HLE. Keep the address range
    // in sync with the layout used in GBAMemory::InitializeHLEBIOS.
    // Minimal trampoline: 6 words at 0x3F00–0x3F17.
    const bool inIrqTrampoline =
        (pcAligned >= 0x00003F00u && pcAligned < 0x00003F18u);

    if (!inIrqVector && !inIrqTrampoline) {
      ExecuteBIOSFunction(pcAligned);
      return;
    }
  }

  if (thumbMode) {
    g_memoryForLog = &memory;
    g_thumbModeForLog = true;

    // Focused Thumb fetch trace for the PCs where we see 0x4698 clobber R8.
    // This helps detect if we're reading the wrong halfword due to PC
    // alignment/masking.
    const uint32_t pcAligned = pc & ~1u;
    if (kEnableHeavyCpuTraces &&
        (pcAligned == 0x08007320 || pcAligned == 0x0800705A ||
         pcAligned == 0x08007BBC)) {
      const uint16_t opAligned = memory.ReadInstruction16(pcAligned);
      const uint16_t opFlipped = memory.ReadInstruction16(pcAligned ^ 1u);
      Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                "THUMB FETCH pc=0x%08x aligned=0x%08x "
                                "opAligned=0x%04x opFlipped=0x%04x T=%d",
                                pc, pcAligned, opAligned, opFlipped,
                                thumbMode ? 1 : 0);
    }

    // In Thumb state, instruction fetch is halfword-aligned.
    // Always fetch from aligned address so we don't accidentally read a
    // swapped/invalid opcode.
    const uint32_t instrAddr = pcAligned;

    // Pipeline emulation: Use prefetched instruction if available.
    // On real ARM7TDMI, writes to upcoming instructions don't affect
    // already-prefetched opcodes (3-stage pipeline behavior).
    uint16_t instruction;
    if (prefetchValid[0] && prefetchThumb[0] && prefetchAddr[0] == instrAddr) {
      // Use prefetched instruction (may differ from current memory if SMC)
      instruction = static_cast<uint16_t>(prefetch[0] & 0xFFFF);

      // Shift pipeline: move slot 1 to slot 0
      prefetch[0] = prefetch[1];
      prefetchAddr[0] = prefetchAddr[1];
      prefetchThumb[0] = prefetchThumb[1];
      prefetchValid[0] = prefetchValid[1];

      // Fetch next instruction into slot 1 (2 instructions ahead of current)
      // ARM7TDMI has 3-stage pipeline: fetch→decode→execute
      // After executing instrAddr, slot 0 has instrAddr+2, need instrAddr+4
      const uint32_t nextAddr = instrAddr + 4u;
      prefetch[1] = memory.ReadInstruction16(nextAddr);
      prefetchAddr[1] = nextAddr;
      prefetchThumb[1] = true;
      prefetchValid[1] = true;
    } else {
      // Pipeline miss or first instruction - fetch directly and refill pipeline
      instruction = memory.ReadInstruction16(instrAddr);

      // Fill pipeline for next instructions
      prefetch[0] = memory.ReadInstruction16(instrAddr + 2u);
      prefetchAddr[0] = instrAddr + 2u;
      prefetchThumb[0] = true;
      prefetchValid[0] = true;

      prefetch[1] = memory.ReadInstruction16(instrAddr + 4u);
      prefetchAddr[1] = instrAddr + 4u;
      prefetchThumb[1] = true;
      prefetchValid[1] = true;
    }

    // Focused trace: the early IRQ/dispatch window where LR becomes even and BX
    // drops into ARM.
    if (kEnableHeavyCpuTraces &&
        ((instrAddr >= 0x080014B0 && instrAddr <= 0x080014C0) ||
         (instrAddr >= 0x080016A0 && instrAddr <= 0x080016D0))) {
      static int windowTraceCount = 0;
      if (windowTraceCount++ < 400) {
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 WIN PC=0x%08x op=0x%04x SP=0x%08x LR=0x%08x R0=0x%08x "
            "R3=0x%08x CPSR=0x%08x T=%d",
            instrAddr, instruction, registers[13], registers[14], registers[0],
            registers[3], cpsr, thumbMode ? 1 : 0);
      }
    }

    // Capture authoritative instruction context for tracing.
    currentInstrAddr = instrAddr;
    currentInstrThumb = true;
    currentOp16 = instruction;
    currentOp32 = 0;

    // Advance to next instruction. Keep the Thumb-visible PC semantics
    // consistent by maintaining R15 as the current instruction address (not
    // PC+4). When an instruction uses PC as an operand, compute it as instrAddr
    // + 4. Compute Thumb-visible PC (used by instructions that read PC). In
    // Thumb state, PC reads as current instruction address + 4.
    const uint32_t pcValue = (instrAddr + 4u);

    // Advance to next instruction.
    registers[15] = instrAddr + 2u;
    DecodeThumb(instruction, pcValue);
  } else {
    g_memoryForLog = &memory;
    g_thumbModeForLog = false;
    const uint32_t instrAddr = pc & ~3u;

    // Pipeline emulation: Use prefetched instruction if available.
    // On real ARM7TDMI, writes to upcoming instructions don't affect
    // already-prefetched opcodes (3-stage pipeline behavior).
    uint32_t instruction;

    if (prefetchValid[0] && !prefetchThumb[0] && prefetchAddr[0] == instrAddr) {
      // Use prefetched instruction (may differ from current memory if SMC)
      instruction = prefetch[0];

      // Shift pipeline: move slot 1 to slot 0
      prefetch[0] = prefetch[1];
      prefetchAddr[0] = prefetchAddr[1];
      prefetchThumb[0] = prefetchThumb[1];
      prefetchValid[0] = prefetchValid[1];

      // Fetch next instruction into slot 1 (2 instructions ahead of current)
      // ARM7TDMI has 3-stage pipeline: fetch→decode→execute
      // After executing instrAddr, slot 0 has instrAddr+4, need instrAddr+8
      const uint32_t nextAddr = instrAddr + 8u;
      prefetch[1] = memory.ReadInstruction32(nextAddr);
      prefetchAddr[1] = nextAddr;
      prefetchThumb[1] = false;
      prefetchValid[1] = true;
    } else {
      // Pipeline miss or first instruction - fetch directly and refill pipeline
      instruction = memory.ReadInstruction32(instrAddr);

      // Fill pipeline for next instructions
      prefetch[0] = memory.ReadInstruction32(instrAddr + 4u);
      prefetchAddr[0] = instrAddr + 4u;
      prefetchThumb[0] = false;
      prefetchValid[0] = true;

      prefetch[1] = memory.ReadInstruction32(instrAddr + 8u);
      prefetchAddr[1] = instrAddr + 8u;
      prefetchThumb[1] = false;
      prefetchValid[1] = true;
    }

    // Capture authoritative instruction context for tracing.
    currentInstrAddr = instrAddr;
    currentInstrThumb = false;
    currentOp16 = 0;
    currentOp32 = instruction;

    registers[15] = instrAddr + 4u;
    Decode(instruction);
  }
}

void ARM7TDMI::StepBack() {
  if (cpuHistory.size() < 2)
    return; // Need previous state
  // Pop current snapshot, restore previous
  cpuHistory.pop_back();
  auto &s = cpuHistory.back();
  // Restore IWRAM first
  // Lazy IWRAM snapshot: capture current IWRAM when debugger actually uses
  // StepBack This way IWRAM snapshotting only happens in debugger mode, not
  // during normal play
  if (s.iwram.empty()) {
    s.iwram.resize(0x8000);
    for (size_t off = 0; off < 0x8000; ++off) {
      s.iwram[off] = memory.Read8(0x03000000 + (uint32_t)off);
    }
  } else {
    // Restore IWRAM from previous snapshot
    for (size_t off = 0; off < s.iwram.size(); ++off) {
      memory.Write8(0x03000000 + (uint32_t)off, s.iwram[off]);
    }
  }
  for (int i = 0; i < 16; ++i)
    registers[i] = s.registers[i];
  cpsr = s.cpsr;
  spsr = s.spsr;
  thumbMode = s.thumbMode;
  halted = true; // stay halted in debugger
  debuggerHalt = true;
  sleepHalt = false;
}

// Debugger API implementations
void ARM7TDMI::AddBreakpoint(uint32_t addr) { breakpoints.push_back(addr); }
void ARM7TDMI::RemoveBreakpoint(uint32_t addr) {
  breakpoints.erase(std::remove(breakpoints.begin(), breakpoints.end(), addr),
                    breakpoints.end());
}
void ARM7TDMI::ClearBreakpoints() { breakpoints.clear(); }
const std::vector<uint32_t> &ARM7TDMI::GetBreakpoints() const {
  return breakpoints;
}
void ARM7TDMI::SetSingleStep(bool enabled) { singleStep = enabled; }
bool ARM7TDMI::IsSingleStep() const { return singleStep; }
void ARM7TDMI::Continue() {
  halted = false;
  sleepHalt = false;
  debuggerHalt = false;
}
void ARM7TDMI::DumpState(std::ostream &os) const {
  os << std::hex;
  os << "CPSR=0x" << cpsr << " Thumb=" << thumbMode << "\n";
  for (int i = 0; i < 16; ++i)
    os << "R" << i << "=0x" << registers[i] << (i == 15 ? "" : " ");
  os << std::dec << "\n";
  // Dump a small window of ROM around PC
  uint32_t pc = registers[15];
  os << "Disasm window around PC:" << "\n";
  for (int i = -8; i <= 8; i += (thumbMode ? 2 : 4)) {
    uint32_t addr = pc + i;
    if (addr >= 0x08000000) {
      if (thumbMode) {
        uint16_t op = memory.Read16(addr);
        os << "  0x" << std::hex << addr << ": 0x" << op
           << (i == 0 ? " <--" : "") << std::dec << "\n";
      } else {
        uint32_t op = memory.Read32(addr);
        os << "  0x" << std::hex << addr << ": 0x" << op
           << (i == 0 ? " <--" : "") << std::dec << "\n";
      }
    }
  }
}

void ARM7TDMI::Fetch() {
  // Pipeline stage implementation
}

void ARM7TDMI::Decode(uint32_t instruction) {
  // Extract Condition Code (CPSR[31:28])
  uint32_t cond =
      ExtractBits(instruction, ARMInstructionFormat::COND_SHIFT, 0xF);

  if (cond != Condition::AL) { // Optimization: AL (Always) is most common
    if (!ConditionSatisfied(cond, cpsr)) {
      return; // Condition failed, instruction acts as NOP
    }
  }

  // Identify Instruction Type based on GBATEK specification
  // Branch and Exchange (BX): xxxx 0001 0010 xxxx xxxx xxxx 0001 xxxx
  if ((instruction & ARMInstructionFormat::BX_MASK) ==
      ARMInstructionFormat::BX_PATTERN) {
    ExecuteBX(instruction);
  }
  // Branch: xxxx 101x xxxx xxxx xxxx xxxx xxxx xxxx
  else if ((instruction & ARMInstructionFormat::B_MASK) ==
           ARMInstructionFormat::B_PATTERN) {
    ExecuteBranch(instruction);
  }
  // Multiply: xxxx 0000 00xx xxxx xxxx 1001 xxxx
  else if ((instruction & ARMInstructionFormat::MUL_MASK) ==
           ARMInstructionFormat::MUL_PATTERN) {
    ExecuteMultiply(instruction);
  }
  // Multiply Long: xxxx 0000 1xxx xxxx xxxx 1001 xxxx
  else if ((instruction & ARMInstructionFormat::MULL_MASK) ==
           ARMInstructionFormat::MULL_PATTERN) {
    ExecuteMultiplyLong(instruction);
  }
  // SWP/SWPB: xxxx 0001 0x00 xxxx xxxx 0000 1001 xxxx
  else if ((instruction & 0x0FB00FF0) == 0x01000090) {
    ExecuteSWP(instruction);
  }
  // Halfword / Signed Data Transfer: (bits27-25=000, bits7=1, bit4=1)
  else if ((instruction & 0x0E000090) == 0x00000090) {
    ExecuteHalfwordDataTransfer(instruction);
  }
  // MRS: xxxx 0001 0x00 1111 xxxx 0000 0000 0000
  else if ((instruction & 0x0FBF0FFF) == 0x010F0000) {
    ExecuteMRS(instruction);
  }
  // MSR (Register): xxxx 0001 0x10 xxxx 1111 0000 0000 xxxx
  else if ((instruction & 0x0FB0FFF0) == 0x0120F000) {
    ExecuteMSR(instruction);
  }
  // MSR (Immediate): xxxx 0011 0x10 xxxx 1111 xxxx xxxx
  else if ((instruction & 0x0FB0F000) == 0x0320F000) {
    ExecuteMSR(instruction);
  }
  // Data Processing: xxxx 00xx xxxx xxxx xxxx xxxx xxxx xxxx
  else if ((instruction & ARMInstructionFormat::DP_MASK) ==
           ARMInstructionFormat::DP_PATTERN) {
    ExecuteDataProcessing(instruction);
  }
  // Single Data Transfer: xxxx 01xx xxxx xxxx xxxx xxxx xxxx xxxx
  else if ((instruction & ARMInstructionFormat::SDT_MASK) ==
           ARMInstructionFormat::SDT_PATTERN) {
    ExecuteSingleDataTransfer(instruction);
  }
  // Block Data Transfer: xxxx 100x xxxx xxxx xxxx xxxx xxxx xxxx
  else if ((instruction & ARMInstructionFormat::BDT_MASK) ==
           ARMInstructionFormat::BDT_PATTERN) {
    ExecuteBlockDataTransfer(instruction);
  }
  // Software Interrupt: xxxx 1111 xxxx xxxx xxxx xxxx xxxx xxxx
  else if ((instruction & ARMInstructionFormat::SWI_MASK) ==
           ARMInstructionFormat::SWI_PATTERN) {
    // ARM SWI has a 24-bit immediate.
    // Commercial ROMs commonly encode the SWI number in the *upper* byte of
    // the immediate (e.g. 0xEF110000 for SWI 0x11), while others use the more
    // conventional low-byte encoding (e.g. 0xEF000011).
    const uint32_t imm24 = (instruction & 0x00FFFFFFu);
    uint32_t swi = (imm24 & 0xFFu);
    if (swi == 0u) {
      swi = ((imm24 >> 16) & 0xFFu);
    }
    ExecuteSWI(swi);
  } else {
    std::cout << "Unknown Instruction: 0x" << std::hex << instruction
              << " at PC=" << (registers[15] - 4)
              << " Mode=" << (thumbMode ? "Thumb" : "ARM") << std::endl;
  }
}

void ARM7TDMI::Execute() {
  // Pipeline stage implementation
}

void ARM7TDMI::ExecuteBranch(uint32_t instruction) {
  // ARM Branch Format: Cond[31:28] | 101[27:25] | L[24] | Offset[23:0]
  bool link = (instruction & ARMInstructionFormat::BL_BIT) != 0;
  int32_t offset = ExtractBranchOffset(instruction);

  // Shift left by 2 (word aligned)
  offset <<= 2;

  // PC behavior: register[15] is already +4 from fetch, so target = (PC + 4) +
  // offset
  uint32_t currentPC = registers[15];

  if (link) {
    registers[Register::LR] =
        currentPC; // LR = PC (instruction following branch)
  }

  uint32_t target = currentPC + 4 + offset;

  // DEBUG: Trace branches in audio code (DISABLED - too verbose)
  // if (currentPC >= 0x30032b0 && currentPC <= 0x3003400) {
  //   std::cout << "[BRANCH DEBUG] PC=0x" << std::hex << (currentPC - 4)
  //             << " instr=0x" << instruction << " offset=" << offset
  //             << " target=0x" << target << std::dec << std::endl;
  // }

  LogBranch(currentPC - 4, target); // Log from actual instruction address
  registers[Register::PC] = target;
  FlushPipeline(); // Branch invalidates prefetched instructions
}

void ARM7TDMI::ExecuteDataProcessing(uint32_t instruction) {
  const bool I = (instruction & ARMInstructionFormat::DP_I_BIT) != 0;
  const uint32_t opcode =
      ExtractBits(instruction, ARMInstructionFormat::DP_OPCODE_SHIFT, 0xF);
  const bool S = (instruction & ARMInstructionFormat::DP_S_BIT) != 0;
  const uint32_t rn =
      ExtractRegisterField(instruction, ARMInstructionFormat::DP_RN_SHIFT);
  const uint32_t rd =
      ExtractRegisterField(instruction, ARMInstructionFormat::DP_RD_SHIFT);

  uint32_t op2 = 0;
  bool shifterCarry = CarryFlagSet(cpsr);

  // Calculate Operand 2
  if (I) {
    // Immediate operand with rotate right by even number of bits
    uint32_t rotate = ExtractBits(instruction, 8, 0xF);
    uint32_t imm = instruction & 0xFF;
    uint32_t shift = rotate * 2;
    if (shift == 0) {
      op2 = imm;
    } else {
      op2 = RotateRight(imm, shift, cpsr, false);
      shifterCarry = (op2 >> 31) & 1;
    }
  } else {
    // Register with optional shift
    uint32_t rm = ExtractRegisterField(instruction, 0);
    uint32_t rmVal = registers[rm];
    if (rm == Register::PC)
      rmVal += 4; // PC is Instruction + 8 (registers[15] is Instruction + 4)

    bool shiftByReg = (instruction >> 4) & 1;
    uint32_t shiftType = ExtractBits(instruction, 5, 0x3);
    uint32_t shiftAmount = 0;

    if (shiftByReg) {
      uint32_t rs = ExtractRegisterField(instruction, 8);
      shiftAmount = registers[rs] & 0xFF;
    } else {
      shiftAmount = ExtractBits(instruction, 7, 0x1F);
      // Immediate shift: encoding of 0 means 32 for LSR and ASR
      if (shiftAmount == 0 &&
          (shiftType == Shift::LSR || shiftType == Shift::ASR)) {
        shiftAmount = 32;
      }
    }

    // Use a temp CPSR to compute shifter carry without mutating flags yet
    uint32_t tempCpsr = cpsr;
    op2 = BarrelShift(rmVal, shiftType, shiftAmount, tempCpsr, true);
    shifterCarry = CarryFlagSet(tempCpsr);
  }

  uint32_t result = 0;
  uint32_t rnVal = registers[rn];
  if (rn == Register::PC)
    rnVal += 4; // PC is Instruction + 8 (registers[15] is Instruction + 4)
  bool carry = CarryFlagSet(cpsr);
  bool overflow = OverflowFlagSet(cpsr);
  bool cOut = carry;

  switch (opcode) {
  case DPOpcode::AND: // AND
    result = rnVal & op2;
    break;
  case DPOpcode::EOR: // EOR
    result = rnVal ^ op2;
    break;
  case DPOpcode::SUB: // SUB
  case DPOpcode::CMP: // CMP
    result = rnVal - op2;
    cOut = (rnVal >= op2);
    {
      bool sign1 = (rnVal >> 31) & 1;
      bool sign2 = (op2 >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 != sign2 && sign1 != signR);
    }
    break;
  case DPOpcode::RSB: // RSB
    result = op2 - rnVal;
    cOut = (op2 >= rnVal); // No Borrow
    {
      bool sign1 = (op2 >> 31) & 1;
      bool sign2 = (rnVal >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 != sign2 && sign1 != signR);
    }
    break;
  case DPOpcode::ADD: // ADD
  case DPOpcode::CMN: // CMN
    result = rnVal + op2;
    cOut = (result < rnVal); // Carry
    {
      bool sign1 = (rnVal >> 31) & 1;
      bool sign2 = (op2 >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 == sign2 && sign1 != signR);
    }
    break;
  case DPOpcode::ADC: // ADC
  {
    uint64_t result64 =
        static_cast<uint64_t>(rnVal) + static_cast<uint64_t>(op2) + carry;
    result = static_cast<uint32_t>(result64);
    cOut = result64 > 0xFFFFFFFFULL;
    overflow = ((rnVal ^ result) & (op2 ^ result)) >> 31;
    break;
  }
  case DPOpcode::SBC: // SBC
    result = rnVal - op2 - !carry;
    cOut = (rnVal >= (uint64_t)op2 + !carry); // No Borrow
    {
      bool sign1 = (rnVal >> 31) & 1;
      bool sign2 = (op2 >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 != sign2 && sign1 != signR);
    }
    break;
  case DPOpcode::RSC: // RSC
    result = op2 - rnVal - !carry;
    cOut = (op2 >= (uint64_t)rnVal + !carry); // No Borrow
    {
      bool sign1 = (op2 >> 31) & 1;
      bool sign2 = (rnVal >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 != sign2 && sign1 != signR);
    }
    break;
  case DPOpcode::TST: // TST
    result = rnVal & op2;
    break;
  case DPOpcode::TEQ: // TEQ
    result = rnVal ^ op2;
    break;
  case DPOpcode::ORR: // ORR
    result = rnVal | op2;
    break;
  case DPOpcode::MOV: // MOV
    result = op2;
    break;
  case DPOpcode::BIC: // BIC
    result = rnVal & (~op2);
    break;
  case DPOpcode::MVN: // MVN
    result = ~op2;
    break;
  }

  // Logical ops use shifter carry (ARM spec); keep previous behavior fallback
  // if not updated above
  switch (opcode) {
  case DPOpcode::AND:
  case DPOpcode::EOR:
  case DPOpcode::TST:
  case DPOpcode::TEQ:
  case DPOpcode::ORR:
  case DPOpcode::MOV:
  case DPOpcode::BIC:
  case DPOpcode::MVN:
    cOut = shifterCarry;
    break;
  default:
    break;
  }

  // Write back result if not a test instruction
  if (opcode != DPOpcode::TST && opcode != DPOpcode::TEQ &&
      opcode != DPOpcode::CMP && opcode != DPOpcode::CMN) {
    if (rd == Register::PC) {
      // ARM state: writing to PC ignores the low 2 bits.
      // For the S=1 return path, we will re-align after CPSR restore based on
      // the restored T bit.
      if (!S) {
        result &= ~3u;
      }
      // DEBUG: Log PC writes during IRQ return
      if (S && opcode == DPOpcode::SUB) {
        uint32_t currentMode = GetCPUMode(cpsr);
        if (kEnableHeavyCpuTraces) {
          std::cerr << "[IRQ RETURN] Mode=0x" << std::hex << currentMode
                    << " LR(r14)=0x" << registers[Register::LR] << " LR_irq=0x"
                    << r14_irq << " result=0x" << result << " CPSR(before)=0x"
                    << cpsr << " SPSR=0x" << spsr
                    << " thumbMode(before)=" << (thumbMode ? 1 : 0) << std::dec
                    << std::endl;
        }
      }
      const uint32_t from = registers[15] - 4;
      TracePCWrite("ALU", from, result, instruction, (uint32_t)opcode,
                   (uint32_t)S);
      LogBranch(from, result);
    }
    registers[rd] = result;
    if (rd == Register::PC) {
      FlushPipeline(); // Data processing to PC invalidates prefetch
    }
  }

  // Update Flags (CPSR) if S is set
  if (S) {
    if (rd == Register::PC) {
      // ARM spec: data-processing with S and Rd==PC restores CPSR from current
      // mode's SPSR. Do not special-case System mode here; System mode has no
      // SPSR.
      uint32_t spsrCopy = spsr;
      const uint32_t oldMode = GetCPUMode(cpsr);

      // BIOS IRQ trampoline may temporarily switch to System mode to run the
      // user handler, then return via `SUBS PC, LR, #4`. In that case
      // System/User has no SPSR, but the return state is still held in the IRQ
      // banked SPSR.
      if ((oldMode == CPUMode::USER || oldMode == CPUMode::SYSTEM) &&
          (currentInstrAddr >= 0x180 && currentInstrAddr < 0x1d0)) {
        const uint32_t modeFromSpsr = GetCPUMode(spsrCopy);
        if (modeFromSpsr != CPUMode::USER && modeFromSpsr != CPUMode::SYSTEM &&
            modeFromSpsr != CPUMode::IRQ &&
            modeFromSpsr != CPUMode::SUPERVISOR) {
          spsrCopy = spsr_irq;
        }
      }

      const uint32_t newMode = GetCPUMode(spsrCopy);
      // Defensive: an invalid SPSR (mode==0) will corrupt CPSR/Thumb state and
      // cascade.
      if (newMode != CPUMode::USER && newMode != CPUMode::SYSTEM &&
          newMode != CPUMode::IRQ && newMode != CPUMode::SUPERVISOR) {
        halted = true;
        sleepHalt = false;
        debuggerHalt = false;
        FILE *logFile = fopen("crash_log.txt", "a");
        if (logFile) {
          fprintf(logFile,
                  "==== Emulator Crash (Invalid SPSR on CPSR restore) ====\n");
          fprintf(logFile,
                  "Reason: DP S+Rd==PC restoring CPSR from invalid SPSR\n");
          fprintf(logFile, "PC: 0x%08X\n", registers[15]);
          fprintf(logFile, "InstrAddr: 0x%08X\n", currentInstrAddr);
          fprintf(logFile, "InstrThumb: %d\n", currentInstrThumb ? 1 : 0);
          fprintf(logFile, "Op16: 0x%04X\n", (unsigned)currentOp16);
          fprintf(logFile, "Op32: 0x%08X\n", currentOp32);
          fprintf(logFile, "CPSR(before): 0x%08X (mode=0x%02X)\n", cpsr,
                  (unsigned)oldMode);
          fprintf(logFile, "SPSR(value):  0x%08X (mode=0x%02X)\n", spsrCopy,
                  (unsigned)newMode);
          fprintf(logFile, "SPSR.irq:    0x%08X (mode=0x%02X)\n", spsr_irq,
                  (unsigned)GetCPUMode(spsr_irq));
          fprintf(logFile, "LR: 0x%08X SP: 0x%08X\n", registers[14],
                  registers[13]);
          fclose(logFile);
        }
        if (CrashPopupCallback)
          CrashPopupCallback("crash_log.txt");
        return;
      }
      // IMPORTANT: SwitchMode() keys off current CPSR mode to decide which bank
      // to save. Switch banks *before* overwriting CPSR, otherwise the bank
      // swap is skipped.
      if (oldMode != newMode) {
        SwitchMode(newMode);
      }
      cpsr = spsrCopy;
      thumbMode = IsThumbMode(cpsr);

      // Align PC based on the restored state.
      registers[Register::PC] = thumbMode ? (registers[Register::PC] & ~1u)
                                          : (registers[Register::PC] & ~3u);
      FlushPipeline(); // Data processing with S=1 and rd=PC invalidates
                       // prefetch

      // Debug: confirm CPSR restore actually re-enters Thumb when expected.
      if (oldMode == CPUMode::IRQ) {
        static int irqReturnPostLogCount = 0;
        if (kEnableHeavyCpuTraces && irqReturnPostLogCount++ < 30) {
          std::cerr << "[IRQ RETURN POST] PC=0x" << std::hex
                    << registers[Register::PC] << " CPSR(after)=0x" << cpsr
                    << " thumbMode(after)=" << (thumbMode ? 1 : 0) << std::dec
                    << std::endl;
        }
      }
    } else {
      UpdateNZFlags(cpsr, result);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, cOut);
      SetCPSRFlag(cpsr, CPSR::FLAG_V, overflow);
    }
  }
}

void ARM7TDMI::ExecuteSingleDataTransfer(uint32_t instruction) {
  const bool I = (instruction >> 25) & 1;
  const bool P = (instruction >> 24) & 1;
  const bool U = (instruction >> 23) & 1;
  const bool B = (instruction >> 22) & 1;
  const bool W = (instruction >> 21) & 1;
  const bool L = (instruction >> 20) & 1;
  const uint32_t rn = ExtractRegisterField(instruction, 16);
  const uint32_t rd = ExtractRegisterField(instruction, 12);
  uint32_t offset = 0;

  if (!I) {
    offset = instruction & 0xFFF;
  } else {
    // ARM addressing mode 2 (register offset): bits[11:0] encode a shifted
    // register (Rm with optional shift by immediate or by register).
    // Many games (including DKC) rely on the shift (e.g. LSL #2) being applied
    // to the index.
    const uint32_t rm = ExtractRegisterField(instruction, 0);
    const uint32_t shiftType = (instruction >> 5) & 0x3u;
    const bool shiftByReg = ((instruction >> 4) & 1u) != 0;
    uint32_t shiftAmount = 0;
    if (shiftByReg) {
      const uint32_t rs = ExtractRegisterField(instruction, 8);
      shiftAmount = registers[rs] & 0xFFu;
    } else {
      shiftAmount = (instruction >> 7) & 0x1Fu;
      // Immediate shift: encoding of 0 means 32 for LSR and ASR
      if (shiftAmount == 0 &&
          (shiftType == Shift::LSR || shiftType == Shift::ASR)) {
        shiftAmount = 32;
      }
    }

    offset = ARM7TDMIHelpers::BarrelShift(registers[rm], shiftType, shiftAmount,
                                          cpsr, false);
  }

  uint32_t baseAddr = registers[rn];
  if (rn == Register::PC) {
    baseAddr += 4;
  }

  uint32_t targetAddr = baseAddr;
  if (P) {
    if (U)
      targetAddr += offset;
    else
      targetAddr -= offset;
  }

  if (L) {
    // Load
    if (B) {
      const uint32_t old = registers[rd];
      uint32_t val8 = memory.Read8(targetAddr);
      registers[rd] = val8;
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb,
                     currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                     old, registers[rd]);
      }
    } else {
      // ARM ARM: word loads from unaligned addresses are rotated right by
      // 8 * (addr[1:0]) after reading from the aligned word address. Many
      // games (including DKC) rely on this for jump tables in IWRAM.
      const uint32_t alignedAddr = targetAddr & ~3u;
      const uint32_t rawAligned = memory.Read32(alignedAddr);
      uint32_t val = rawAligned;
      const uint32_t rotBytes = (targetAddr & 3u) * 8u;
      if (rotBytes != 0) {
        val = (val >> rotBytes) | (val << (32u - rotBytes));
      }
      if (rd == Register::PC) {
        // ARM state: ignore low 2 bits when loading into PC.
        val &= ~3u;
        const uint32_t from = registers[Register::PC] - 4;
        TracePCWrite("LDR", from, val, instruction, targetAddr, rn);
        LogBranch(from, val);
      }
      const uint32_t old = registers[rd];
      registers[rd] = val;
      if (rd == Register::PC) {
        FlushPipeline(); // LDR to PC invalidates prefetch
      }
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb,
                     currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                     old, registers[rd]);
      }
      if (rd == 11) {
        const uint32_t tracedInstr =
            currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
        TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                      registers[rd]);
      }
    }
  } else {
    // Store
    uint32_t val = registers[rd];
    if (rd == Register::PC)
      val += 8; // PC+12 (registers[15] is PC+4)

    if (B) {
      memory.Write8(targetAddr, val & 0xFF);
    } else {
      TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                        currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                        targetAddr, val, registers[13]);
      memory.Write32(targetAddr, val);
    }
  }

  if (!P || W) {
    uint32_t newBase = baseAddr;
    if (U)
      newBase += offset;
    else
      newBase -= offset;
    const uint32_t old = registers[rn];
    registers[rn] = newBase;
    if (rn == 8) {
      TraceR8Write(currentInstrAddr, currentInstrThumb,
                   currentInstrThumb ? (uint32_t)currentOp16 : currentOp32, old,
                   registers[rn]);
    }
    if (rn == 11) {
      const uint32_t tracedInstr =
          currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
      TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                    registers[rn]);
    }
  }
}

void ARM7TDMI::ExecuteBlockDataTransfer(uint32_t instruction) {
  const bool P = (instruction >> 24) & 1;
  const bool U = (instruction >> 23) & 1;
  const bool S = (instruction >> 22) & 1;
  const bool W = (instruction >> 21) & 1;
  const bool L = (instruction >> 20) & 1;
  const uint32_t rn = ExtractRegisterField(instruction, 16);
  uint16_t regList = instruction & 0xFFFF;

  // PC-relative logging: in ARM state, registers[15] is already PC+8 after
  // prefetch increment.
  const uint32_t instrAddr = registers[15] - 8;

  const uint32_t base =
      (rn == Register::PC) ? (registers[Register::PC] - 8 + 8) : registers[rn];
  uint32_t startAddress = base;

  // Count set bits
  int numRegs = 0;
  for (int i = 0; i < 16; ++i) {
    if ((regList >> i) & 1)
      numRegs++;
  }

  // Calculate start address based on addressing mode (ARM ARM).
  // IA (P=0,U=1): start = Rn
  // IB (P=1,U=1): start = Rn + 4
  // DA (P=0,U=0): start = Rn - 4*(n-1)
  // DB (P=1,U=0): start = Rn - 4*n
  if (U) {
    startAddress = base + (P ? 4u : 0u);
  } else {
    startAddress =
        base - (P ? (uint32_t)numRegs * 4u : (uint32_t)(numRegs - 1) * 4u);
  }

  uint32_t currentAddr = startAddress;
  bool writeBack = W;

  // If we're about to load PC and the computed address is clearly wrong, log
  // it. This is focused on SMA2's early crash: LDMDA R8, {...,PC}.
  // NOTE: Disabled for now as it's too noisy
  /*
  if (L && ((regList >> 15) & 1)) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "LDM setup: instr=0x%08x Rn=R%u base=0x%08x "
                              "start=0x%08x P=%d U=%d W=%d S=%d regList=0x%04x",
                              instruction, (unsigned)rn, base, startAddress,
                              (int)P, (int)U, (int)W, (int)S,
                              (unsigned)regList);
  }
  */

  // Handle S bit:
  // - If LDM with S=1 and PC not in list: access user-mode banked regs (R8-R14)
  // without changing CPSR.
  // - If LDM with S=1 and PC in list: restore CPSR from SPSR and then load regs
  // as normal.
  const uint32_t curMode = GetCPUMode(cpsr);
  const bool pcInList = ((regList >> 15) & 1) != 0;
  const bool userMode =
      S && !pcInList && curMode != CPUMode::USER && curMode != CPUMode::SYSTEM;

  for (int i = 0; i < 16; ++i) {
    if ((regList >> i) & 1) {
      if (L) {
        // Load (LDM)
        uint32_t val = memory.Read32(currentAddr);
        TraceWatchRead32(instrAddr, false, instruction, currentAddr, val,
                         registers[13]);

        // If we're loading into PC, canonicalize the address.
        // Some code relies on bit0 for Thumb state; memory fetch is always
        // aligned.
        if (i == 15) {
          // Align later based on restored CPSR for LDM^, otherwise word-align.
          val &= ~3u;
        }
        if (i == 15) {
          TracePCWrite("LDM", instrAddr, val, instruction, currentAddr,
                       regList);
          LogBranch(instrAddr, val);
        }

        // Per ARM ARM: if S=1 and PC in list, restore CPSR from SPSR.
        // Do this *after* the load completes so the load itself uses the
        // correct bank. We'll apply it once at the end of the transfer.

        // Write to register: respect user-mode bank if S bit set
        if (userMode && i >= 13 && i <= 14) {
          // Access user-mode R13/R14 instead of current mode's bank
          if (i == 13)
            r13_usr = val;
          else if (i == 14)
            r14_usr = val;
        } else {
          const uint32_t old = registers[i];
          registers[i] = val;
          if (i == 15 && !S) {
            FlushPipeline(); // LDM to PC (without S bit) invalidates prefetch
          }
          if (i == 8) {
            TraceR8Write(currentInstrAddr, currentInstrThumb,
                         currentInstrThumb ? (uint32_t)currentOp16
                                           : currentOp32,
                         old, registers[i]);
          }
          if (i == 11) {
            const uint32_t tracedInstr =
                currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
            TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                          registers[i]);
          }
        }
        // std::cout << "LDM: R" << i << " <- [0x" << std::hex << currentAddr <<
        // "] (0x" << registers[i] << ")" << std::endl;
      } else {
        // Store (STM)
        uint32_t val;

        // Read from register: respect user-mode bank if S bit set
        if (userMode && i >= 13 && i <= 14) {
          // Access user-mode R13/R14 instead of current mode's bank
          if (i == 13)
            val = r13_usr;
          else if (i == 14)
            val = r14_usr;
        } else {
          val = registers[i];
        }

        if (i == 15)
          val += 4; // PC store quirk? Usually PC+12

        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          currentInstrThumb ? (uint32_t)currentOp16
                                            : currentOp32,
                          currentAddr, val, registers[13]);
        memory.Write32(currentAddr, val);
        // std::cout << "STM: [0x" << std::hex << currentAddr << "] <- R" << i
        // << " (0x" << val << ")" << std::endl;
      }
      currentAddr += 4;
    }
  }

  // Apply CPSR restore for LDM^ with PC in list.
  if (L && S && pcInList) {
    const uint32_t spsrCopy = spsr;
    const uint32_t oldMode = GetCPUMode(cpsr);
    const uint32_t newMode = GetCPUMode(spsrCopy);
    if (newMode != CPUMode::USER && newMode != CPUMode::SYSTEM &&
        newMode != CPUMode::IRQ && newMode != CPUMode::SUPERVISOR) {
      halted = true;
      sleepHalt = false;
      debuggerHalt = false;
      FILE *logFile = fopen("crash_log.txt", "a");
      if (logFile) {
        fprintf(
            logFile,
            "==== Emulator Crash (Invalid SPSR on LDM^ CPSR restore) ====\n");
        fprintf(logFile, "PC: 0x%08X\n", registers[15]);
        fprintf(logFile, "InstrAddr: 0x%08X\n", currentInstrAddr);
        fprintf(logFile, "InstrThumb: %d\n", currentInstrThumb ? 1 : 0);
        fprintf(logFile, "Op16: 0x%04X\n", (unsigned)currentOp16);
        fprintf(logFile, "Op32: 0x%08X\n", currentOp32);
        fprintf(logFile, "CPSR(before): 0x%08X (mode=0x%02X)\n", cpsr,
                (unsigned)oldMode);
        fprintf(logFile, "SPSR(value):  0x%08X (mode=0x%02X)\n", spsrCopy,
                (unsigned)newMode);
        fclose(logFile);
      }
      if (CrashPopupCallback)
        CrashPopupCallback("crash_log.txt");
      return;
    }
    // See note above: bank switch must occur before CPSR overwrite.
    if (oldMode != newMode) {
      SwitchMode(newMode);
    }
    cpsr = spsrCopy;
    thumbMode = IsThumbMode(cpsr);

    // Align PC based on the restored state.
    registers[Register::PC] = thumbMode ? (registers[Register::PC] & ~1u)
                                        : (registers[Register::PC] & ~3u);
    FlushPipeline(); // LDM with PC and S=1 invalidates prefetch
  }

  // Writeback
  // For LDM, if Rn is in the list, writeback is ignored (loaded value persists)
  // For STM, writeback always happens (base is updated)
  if (writeBack) {
    const bool rnInList = ((regList >> rn) & 1) != 0;
    if (!L || !rnInList) {
      // Final address after transfer is always base +/- 4*numRegs.
      const uint32_t old = registers[rn];
      registers[rn] =
          U ? (base + (uint32_t)numRegs * 4u) : (base - (uint32_t)numRegs * 4u);
      if (rn == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb,
                     currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                     old, registers[rn]);
      }
      if (rn == 11) {
        const uint32_t tracedInstr =
            currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
        TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                      registers[rn]);
      }
    }
  }
}

void ARM7TDMI::ExecuteHalfwordDataTransfer(uint32_t instruction) {
  const bool P = (instruction >> 24) & 1;
  const bool U = (instruction >> 23) & 1;
  const bool I = (instruction >> 22) & 1;
  const bool W = (instruction >> 21) & 1;
  const bool L = (instruction >> 20) & 1;
  const uint32_t rn = ExtractRegisterField(instruction, 16);
  const uint32_t rd = ExtractRegisterField(instruction, 12);
  uint32_t offset = 0;
  uint32_t opcode = (instruction >> 5) & 0x3; // H, SB, SH

  if (I) {
    // Immediate Offset
    offset = ((instruction >> 4) & 0xF0) | (instruction & 0xF);
  } else {
    // Register Offset
    uint32_t rm = ExtractRegisterField(instruction, 0);
    offset = registers[rm];
  }

  uint32_t baseAddr = registers[rn];
  if (rn == Register::PC)
    baseAddr += 4; // PC+8 (registers[15] is PC+4)

  uint32_t targetAddr = baseAddr;
  if (P) {
    if (U)
      targetAddr += offset;
    else
      targetAddr -= offset;
  }

  if (L) {
    // Load
    if (opcode == 1) { // LDRH (Unsigned Halfword)
      // ARM7TDMI: LDRH from odd address reads aligned halfword and rotates by 8
      if (targetAddr & 1) {
        uint16_t value = memory.Read16(targetAddr & ~1u);
        registers[rd] = (value >> 8) | (value << 24);
      } else {
        uint16_t value = memory.Read16(targetAddr);
        registers[rd] = value;
      }
    } else if (opcode == 2) { // LDRSB (Signed Byte)
      int8_t val = (int8_t)memory.Read8(targetAddr);
      registers[rd] = (int32_t)val;
    } else if (opcode == 3) { // LDRSH (Signed Halfword)
      // ARM7TDMI: LDRSH from odd address behaves as LDRSB
      if (targetAddr & 1) {
        int8_t val = (int8_t)memory.Read8(targetAddr);
        registers[rd] = (int32_t)val;
      } else {
        int16_t val = (int16_t)memory.Read16(targetAddr);
        registers[rd] = (int32_t)val;
      }
    }
  } else {
    // Store
    if (opcode == 1) { // STRH
      memory.Write16(targetAddr, registers[rd] & 0xFFFF);
    }
  }

  if (!P || W) {
    if (U)
      registers[rn] = baseAddr + offset;
    else
      registers[rn] = baseAddr - offset;
  }
}

void ARM7TDMI::ExecuteBX(uint32_t instruction) {
  uint32_t rm = ExtractRegisterField(instruction, 0);
  uint32_t target = registers[rm];

  LogBranch(registers[15] - 4, target);

  if (target & 1) {
    thumbMode = true;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, true); // Set T bit in CPSR
    registers[Register::PC] = target & 0xFFFFFFFE;
  } else {
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);        // Clear T bit in CPSR
    registers[Register::PC] = target & 0xFFFFFFFC; // Align to 4 bytes
  }
  FlushPipeline(); // BX invalidates prefetched instructions (and may switch
                   // mode)
}

void ARM7TDMI::ExecuteBIOSFunction(uint32_t biosPC) {
  // BIOS HLE: Games call BIOS functions directly by jumping to BIOS ROM
  // (0x0-0x3FFF) We don't have BIOS ROM, so provide HLE implementations based
  // on entry point LR (R14) contains return address - we'll jump back there
  // after HLE function

  // Common BIOS entry points (from GBATEK and reverse engineering):
  // 0x000: Reset
  // 0x188: VBlankIntrWait
  // 0x194: IntrWait
  // 0x1C0: Div
  // 0x1C8: DivArm
  // Many games also call intermediate addresses which are part of BIOS
  // subroutines

  uint32_t returnAddr = registers[14]; // LR holds return address

  // Identify BIOS function by entry point
  // For unknown entry points, just return (no-op)
  switch (biosPC) {
  case 0x018: // IRQ vector entry (HLE)
  {
    // Minimal BIOS IRQ trampoline:
    // - Call user handler at [0x03007FFC]
    // - Return via a BIOS-return stub at 0x01A0
    // Note: The real BIOS does more bookkeeping; this is the smallest
    // faithful behavior needed for games to progress.

    const uint32_t handler = memory.Read32(0x03007FFCu);
    if (handler == 0) {
      // No handler installed; perform an immediate BIOS-style return.
      registers[15] = 0x000001A0;
      return;
    }

    // Real BIOS trampoline executes the user handler in System mode (not IRQ
    // mode) so it uses the user/system banked SP/LR. Many games rely on this.
    // We emulate that here before branching to the handler.
    registers[14] = 0x000001A0; // Return point after handler completes.

    // Switch to System mode while keeping IRQs masked (CPSR.I is already set
    // by the IRQ entry path).
    SwitchMode(CPUMode::SYSTEM);

    // Branch to user handler (BX semantics).
    if (handler & 1u) {
      thumbMode = true;
      SetCPSRFlag(cpsr, CPSR::FLAG_T, true);
      registers[15] = handler & ~1u;
    } else {
      thumbMode = false;
      SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
      registers[15] = handler & ~3u;
    }
    return;
  }

  case 0x1A0: // IRQ return stub (HLE)
  {
    if (!irqStack.empty()) {
      const IrqContext ctx = irqStack.back();
      irqStack.pop_back();

      // Clear BIOS_IF and IF bits (best-effort).
      memory.Write16(0x03007FF8, 0);
      memory.Write16(IORegs::REG_IF, 0xFFFF);

      const uint32_t oldMode = GetCPUMode(cpsr);
      const uint32_t newMode = GetCPUMode(ctx.cpsr);
      if (oldMode != newMode) {
        SwitchMode(newMode);
      }
      cpsr = ctx.cpsr;
      thumbMode = IsThumbMode(cpsr);

      registers[0] = ctx.r0;
      registers[1] = ctx.r1;
      registers[2] = ctx.r2;
      registers[3] = ctx.r3;
      registers[12] = ctx.r12;
      registers[14] = ctx.lr;

      // Resume at the interrupted PC (keep alignment consistent with CPSR.T).
      registers[15] = ctx.pc;
      if (thumbMode) {
        registers[15] &= ~1u;
      } else {
        registers[15] &= ~3u;
      }
      return;
    }
    // No saved context; fall through and just return.
    break;
  }

  case 0x188: // VBlankIntrWait
  {
    // Enable VBlank IRQ in DISPSTAT (Bit 3). Many games assume BIOS does this.
    uint16_t dispstat = memory.Read16(IORegs::REG_DISPSTAT);
    memory.Write16(IORegs::REG_DISPSTAT, dispstat | 0x0008);

    registers[0] = 1;      // discard old flags
    registers[1] = 0x0001; // wait for VBlank
    [[fallthrough]];
  }

  case 0x194: // IntrWait(discard, flags)
  {
    uint32_t discardOld = registers[0];
    uint32_t waitFlags = registers[1];

    uint32_t biosIF = memory.Read16(0x03007FF8);

    if (discardOld) {
      memory.Write16(0x03007FF8, biosIF & ~waitFlags);
    }

    biosIF = memory.Read16(0x03007FF8);

    if (biosIF & waitFlags) {
      memory.Write16(0x03007FF8, biosIF & ~waitFlags);
    } else {
      memory.Write16(IORegs::REG_IME, 1);
      uint16_t ie = memory.Read16(IORegs::REG_IE);
      memory.Write16(IORegs::REG_IE, (uint16_t)(ie | (waitFlags & 0xFFFF)));
      cpsr &= ~0x80;
      halted = true;
      sleepHalt = true;

      registers[15] = biosPC;
      return;
    }
    break;
  }

  case 0x1C0: // Div
  case 0x1C8: // DivArm
  {
    int32_t numerator = (int32_t)registers[0];
    int32_t denominator = (int32_t)registers[1];

    if (denominator == 0) {
      registers[0] = (numerator < 0) ? 1 : -1;
      registers[1] = numerator;
      registers[3] = 1;
    } else {
      int32_t quotient = numerator / denominator;
      int32_t remainder = numerator % denominator;

      registers[0] = quotient;
      registers[1] = remainder;
      registers[3] = (quotient < 0) ? -quotient : quotient;
    }
    break;
  }

  case 0x1F8: // GetBiosChecksum
    registers[0] = 0xBAAE187F;
    break;

  case 0x000: // Reset
    registers[15] = 0x08000000;
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
    FlushPipeline(); // Reset invalidates prefetch
    return;

  default:
    // Unknown BIOS entry point - just return
    // Most unknown addresses are internal BIOS subroutines
    // Returning immediately is safer than crashing
    break;
  }

  // Return to caller by jumping to LR
  if (returnAddr & 1) {
    thumbMode = true;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, true);
    registers[15] = returnAddr & 0xFFFFFFFE;
  } else {
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
    registers[15] = returnAddr & 0xFFFFFFFC;
  }
  FlushPipeline(); // BIOS return invalidates prefetch
}

void ARM7TDMI::ExecuteSWI(uint32_t comment) {
  // A reasonable baseline for SWI entry/exit + minimal BIOS wrapper work.
  // When we HLE a BIOS routine, we skip the real instruction stream, so we
  // must charge some time explicitly.
  constexpr int kSwiOverheadCycles = 32;

  // OGDK TRACE: Log all SWI calls for debugging (with instruction bytes)
  /*
  if (comment != 0x05 && comment != 0x04 &&
      comment != 0x02) { // Skip VBlankIntrWait/IntrWait/HALT spam
    uint32_t instrAddr = registers[15] - (thumbMode ? 2 : 4);
    uint32_t instrBytes = thumbMode ? currentOp16 : currentOp32;
    uint32_t cond = (instrBytes >> 28) & 0xF;
    bool zFlag = (cpsr >> 30) & 1;
    char buf[250];
    snprintf(buf, sizeof(buf),
             "SWI 0x%02X at PC=0x%08X instr=0x%08X cond=%X Z=%d %s r0=0x%08X "
             "r1=0x%08X r2=0x%08X",
             comment, instrAddr, instrBytes, cond, zFlag,
             thumbMode ? "THUMB" : "ARM", registers[0], registers[1],
             registers[2]);
    AIO::Emulator::Common::Logger::Instance().Log(
        AIO::Emulator::Common::LogLevel::Info, "OGDK_SWI", buf);
  }
  */

  // OGDK: Dump IWRAM code around 0x54E0 and 0x56A4 (IRQ handler) at first SWI
  // 0x02
  /*
  {
    uint32_t instrAddr = registers[15] - (thumbMode ? 2 : 4);
    static bool dumpedIwram = false;
    if (!dumpedIwram && comment == 0x02 && instrAddr == 0x030054E0) {
      dumpedIwram = true;
      FILE *f = fopen("/tmp/iwram_code.txt", "w");
      if (f) {
        fprintf(f, "=== IWRAM code dump around 0x54E0 (wait loop/SWI 0x02 "
                   "location) ===\n\n");
        for (int i = -32; i <= 32; i++) {
          uint32_t addr = 0x030054E0 + i * 4;
          uint32_t instr = memory.Read32(addr);
          const char *marker = (i == 0) ? " <-- SWI HERE" : "";
          fprintf(f, "0x%08X: 0x%08X%s\n", addr, instr, marker);
        }
        fprintf(f, "\n=== IWRAM code at 0x030056A4 (IRQ handler) ===\n\n");
        for (int i = -8; i <= 48; i++) {
          uint32_t addr = 0x030056A4 + i * 4;
          uint32_t instr = memory.Read32(addr);
          const char *marker = (i == 0) ? " <-- IRQ HANDLER ENTRY" : "";
          fprintf(f, "0x%08X: 0x%08X%s\n", addr, instr, marker);
        }
        fprintf(f,
                "\n=== IWRAM code at 0x030051CC (target buffer write) ===\n\n");
        for (int i = -20; i <= 20; i++) {
          uint32_t addr = 0x030051CC + i * 4;
          uint32_t instr = memory.Read32(addr);
          const char *marker = (i == 0) ? " <-- TARGET WRITE" : "";
          fprintf(f, "0x%08X: 0x%08X%s\n", addr, instr, marker);
        }

        // Dump VRAM tile data at CharBase 0 and CharBase 1
        fprintf(f, "\n=== VRAM CharBase 0 (0x06000000) first 64 bytes ===\n");
        for (int i = 0; i < 64; i += 4) {
          uint32_t val = memory.Read32(0x06000000 + i);
          fprintf(f, "0x%08X: 0x%08X\n", 0x06000000 + i, val);
        }
        fprintf(f, "\n=== VRAM CharBase 1 (0x06004000) first 64 bytes ===\n");
        for (int i = 0; i < 64; i += 4) {
          uint32_t val = memory.Read32(0x06004000 + i);
          fprintf(f, "0x%08X: 0x%08X\n", 0x06004000 + i, val);
        }

        // Dump VRAM code at 0x060000FC (where BG0CNT is set)
        fprintf(f,
                "\n=== VRAM code at 0x060000FC (BG0CNT write location) ===\n");
        for (int i = -16; i <= 32; i++) {
          uint32_t addr = 0x060000FC + i * 4;
          uint32_t instr = memory.Read32(addr);
          const char *marker = (i == 0) ? " <-- BG0CNT WRITE" : "";
          fprintf(f, "0x%08X: 0x%08X%s\n", addr, instr, marker);
        }

        fclose(f);
        AIO::Emulator::Common::Logger::Instance().Log(
            AIO::Emulator::Common::LogLevel::Info, "OGDK_SWI",
            "Dumped IWRAM code to /tmp/iwram_code.txt");
      }
    }
  }
  */

  switch (comment) {
  case 0x00: // SoftReset
    registers[15] = 0x08000000;
    registers[13] = 0x03007F00; // Reset SP
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
    FlushPipeline(); // SoftReset jumps to ROM entry
    break;
  case 0x01: // RegisterRamReset - Clear/Initialize RAM and registers
  {
    uint8_t flags = registers[0] & 0xFF;

    // OGDK DEBUG: Trace RegisterRamReset - Removed
    /*
    std::cout << "[OGDK_RAMRESET] RegisterRamReset flags=0x" << std::hex
              << (int)flags << ... << std::dec << std::endl;
    */

    // Bit 0: Clear 256K EWRAM (0x02000000-0x0203FFFF)
    if (flags & 0x01) {
      for (uint32_t addr = 0x02000000; addr < 0x02040000; addr += 4) {
        memory.Write32(addr, 0);
      }
    }

    // Bit 1: Clear 32K IWRAM (0x03000000-0x03007FFF, excluding last 0x200
    // bytes)
    if (flags & 0x02) {
      for (uint32_t addr = 0x03000000; addr < 0x03007E00; addr += 4) {
        memory.Write32(addr, 0);
      }
    }

    // Bit 2: Clear Palette RAM (0x05000000-0x050003FF)
    // Note: Palette only supports 16/32-bit writes
    if (flags & 0x04) {
      for (uint32_t addr = 0x05000000; addr < 0x05000400; addr += 4) {
        memory.Write32(addr, 0);
      }
    }

    // Bit 3: Clear VRAM (0x06000000-0x06017FFF)
    // Note: VRAM only supports 16/32-bit writes
    if (flags & 0x08) {
      for (uint32_t addr = 0x06000000; addr < 0x06018000; addr += 4) {
        memory.Write32(addr, 0);
      }
    }

    // Bit 4: Clear OAM (0x07000000-0x070003FF)
    // Note: OAM only supports 16/32-bit writes
    if (flags & 0x10) {
      for (uint32_t addr = 0x07000000; addr < 0x07000400; addr += 4) {
        memory.Write32(addr, 0);
      }
    }

    // Bits 5-6: Reset SIO registers (bit 5=0x04000120-0x04000159, bit
    // 6=0x04000300)
    // TODO: Implement SIO register reset if needed

    // Bit 7: Reset other registers
    if (flags & 0x80) {
      // Reset most IO registers to defaults
      // DISPSTAT, BG control, etc. (do not force blank display)
      // Real BIOS leaves display state for the game to manage; keep DISPCNT
      // unchanged here.
      memory.Write16(IORegs::REG_DISPSTAT, 0x0000);
      memory.Write16(IORegs::REG_BG0CNT, 0x0000);
      memory.Write16(IORegs::REG_BG1CNT, 0x0000);
      memory.Write16(IORegs::REG_BG2CNT, 0x0000);
      memory.Write16(IORegs::REG_BG3CNT, 0x0000);
      // Reset sound registers
      for (uint32_t addr = IORegs::BASE + 0x60; addr <= IORegs::BASE + 0xA6;
           addr += 2) {
        memory.Write16(addr, 0);
      }
      // Reset DMA registers
      for (uint32_t addr = IORegs::BASE + 0xB0; addr <= IORegs::BASE + 0xDE;
           addr += 2) {
        memory.Write16(addr, 0);
      }
    }
    break;
  }
  case 0x02: // Halt
  {
    halted = true;
    sleepHalt = true;
    debuggerHalt = false;
    break;
  }
  case 0x03: // Stop/Sleep
    halted = true;
    sleepHalt = true;
    debuggerHalt = false;
    break;
  case 0x04: // IntrWait
  intrwait_entry: {
    // R0 = Clear Old Flags (1=Clear), R1 = Wait Flags
    uint32_t clearOld = registers[0];
    uint32_t waitFlags = registers[1];

    if (clearOld) {
      uint16_t currentFlags = memory.Read16(0x03007FF8);
      memory.Write16(0x03007FF8,
                     (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)));
      registers[0] = 0;
    }

    uint16_t currentFlags = memory.Read16(0x03007FF8);
    if (currentFlags & (waitFlags & 0xFFFF)) {
      memory.Write16(0x03007FF8,
                     (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)));
      return;
    }

    memory.Write16(IORegs::REG_IME, 1);
    uint16_t ie = memory.Read16(IORegs::REG_IE);
    memory.Write16(IORegs::REG_IE, ie | (waitFlags & 0xFFFF));
    cpsr &= ~0x80;
    halted = true;
    sleepHalt = true;
    debuggerHalt = false;

    if (thumbMode)
      registers[15] -= 2;
    else
      registers[15] -= 4;
    break;
  }
  case 0x05: // VBlankIntrWait
  {
    // VBlankIntrWait is equivalent to:
    // R0 = 1 (clear old flags)
    // R1 = 1 (wait for VBlank IRQ, bit 0)
    // Then call IntrWait (SWI 0x04)

    // Enable VBlank IRQ in DISPSTAT (Bit 3) - Required for VBlank IRQ to fire
    uint16_t dispstat = memory.Read16(IORegs::REG_DISPSTAT);
    memory.Write16(IORegs::REG_DISPSTAT, dispstat | 0x0008);

    // Set up for IntrWait: R0=1, R1=1
    registers[0] = 1; // Clear old flags
    registers[1] = 1; // Wait for VBlank IRQ (bit 0)

    // Fall through to IntrWait logic
    goto intrwait_entry;
  }
  case 0x06: // Div - R0 = R0 / R1, R1 = R0 % R1, R3 = abs(R0 / R1)
  {
    int32_t num = (int32_t)registers[0];
    int32_t denom = (int32_t)registers[1];
    if (denom == 0) {
      // Division by zero
      // R0 = 0 (if R0=0), +1 (if R0>0), -1 (if R0<0)
      // R1 = R0
      // R3 = 0 (if R0=0), +1 (if R0!=0)
      if (num == 0) {
        registers[0] = 0;
        registers[3] = 0;
      } else {
        registers[0] = (num < 0) ? -1 : 1;
        registers[3] = 1;
      }
      registers[1] = num;
    } else {
      int32_t result = num / denom;
      int32_t remainder = num % denom;
      registers[0] = (uint32_t)result;
      registers[1] = (uint32_t)remainder;
      registers[3] = (uint32_t)(result < 0 ? -result : result);
    }
    break;
  }
  case 0x07: // DivArm - Same as Div but with R0 and R1 swapped
  {
    int32_t num = (int32_t)registers[1];
    int32_t denom = (int32_t)registers[0];
    if (denom == 0) {
      if (num == 0) {
        registers[0] = 0;
        registers[3] = 0;
      } else {
        registers[0] = (num < 0) ? -1 : 1;
        registers[3] = 1;
      }
      registers[1] = num;
    } else {
      int32_t result = num / denom;
      int32_t remainder = num % denom;
      registers[0] = (uint32_t)result;
      registers[1] = (uint32_t)remainder;
      registers[3] = (uint32_t)(result < 0 ? -result : result);
    }
    break;
  }
  case 0x08: // Sqrt - R0 = sqrt(R0)
  {
    uint32_t val = registers[0];
    uint32_t result = 0;
    uint32_t bit = 1 << 30; // Start with highest bit

    while (bit > val)
      bit >>= 2;

    while (bit != 0) {
      if (val >= result + bit) {
        val -= result + bit;
        result = (result >> 1) + bit;
      } else {
        result >>= 1;
      }
      bit >>= 2;
    }
    registers[0] = result;
    break;
  }
  case 0x09: // ArcTan - R0 = arctan(R0)
  {
    // R0 is a signed 16.16 fixed-point value representing tan(θ)
    // Result is a signed 16.16 fixed-point angle in range -π/4 to π/4
    int32_t x = (int32_t)registers[0];
    // Convert to double, calculate, convert back
    double tanVal = x / 65536.0;
    double angle = atan(tanVal);
    // Convert to 16.16 fixed point (but result is actually in 1.15 format for
    // BIOS) BIOS returns value in range -0x4000 to 0x4000 (-π/4 to π/4 scaled)
    registers[0] =
        (uint32_t)(int32_t)(angle / (2.0 * 3.14159265358979323846) * 65536.0);
    break;
  }
  case 0x0A: // ArcTan2 - R0 = arctan2(R0, R1)
  {
    // R0 = Y, R1 = X (both signed 16.16 fixed-point)
    // Result is angle from 0 to 2π mapped to 0x0000-0xFFFF
    int32_t y = (int32_t)registers[0];
    int32_t x = (int32_t)registers[1];
    double yVal = y / 65536.0;
    double xVal = x / 65536.0;
    double angle = atan2(yVal, xVal); // Returns -PI to PI

    if (angle < 0) {
      angle += 2.0 * 3.14159265358979323846;
    }

    // Map 0-2PI to 0-FFFF
    uint16_t result =
        (uint16_t)(angle / (2.0 * 3.14159265358979323846) * 65536.0);
    registers[0] = result;
    break;
  }
  case 0x0B: // CpuSet (R0=Src, R1=Dst, R2=Cnt/Ctrl)
  {
    // Real GBA behavior: SWI runs with IRQs masked (CPSR.I=1) and the
    // caller CPSR is restored on return. Without this, our HLE loop can
    // be interrupted mid-copy by an IRQ, which some titles don't expect.
    const uint32_t savedCpsr = cpsr;
    cpsr |= 0x80u;

    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    uint32_t len = registers[2] & 0x1FFFFF;
    bool fixedSrc = (registers[2] >> 24) & 1;
    bool is32Bit = (registers[2] >> 26) & 1;

    // OGDK DEBUG: Trace CpuSet - Removed
    /*
    if ((dst & 0xFF000000) == 0x03000000 && (dst & 0x7FFF) < 0x2000) {
      // Log removed
    }
    */

    int perUnitCycles = is32Bit ? 4 : 2;
    const uint32_t batchSize = 64; // Update PPU every 64 units

    if (is32Bit) {
      uint32_t fixedVal = fixedSrc ? memory.Read32(src) : 0;
      for (uint32_t i = 0; i < len; ++i) {
        uint32_t val = fixedSrc ? fixedVal : memory.Read32(src);
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          currentInstrThumb ? (uint32_t)currentOp16
                                            : currentOp32,
                          dst, val, registers[13]);
        memory.Write32(dst, val);
        dst += 4;
        if (!fixedSrc)
          src += 4;

        // Periodically advance PPU/timers to allow VBlank/HBlank
        if ((i + 1) % batchSize == 0) {
          AdvanceHLECycles(perUnitCycles * (int)batchSize);
        }
      }
    } else {
      uint16_t fixedVal = fixedSrc ? memory.Read16(src) : 0;
      for (uint32_t i = 0; i < len; ++i) {
        uint16_t val = fixedSrc ? fixedVal : memory.Read16(src);
        memory.Write16(dst, val);
        dst += 2;
        if (!fixedSrc)
          src += 2;

        // Periodically advance PPU/timers to allow VBlank/HBlank
        if ((i + 1) % batchSize == 0) {
          AdvanceHLECycles(perUnitCycles * (int)batchSize);
        }
      }
    }

    // Advance remaining cycles for any partial batch
    uint32_t remaining = len % batchSize;
    if (remaining > 0) {
      AdvanceHLECycles(perUnitCycles * (int)remaining);
    }
    // Advance SWI overhead
    AdvanceHLECycles(kSwiOverheadCycles);

    cpsr = savedCpsr;
    break;
  }
  case 0x0C: // CpuFastSet (R0=Src, R1=Dst, R2=Cnt/Ctrl)
  {
    // See CpuSet notes: keep IRQs masked during the bulk transfer.
    const uint32_t savedCpsr = cpsr;
    cpsr |= 0x80u;

    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    // BIOS convention: low 21 bits encode a 32-bit word count, processed in
    // 8-word (32-byte) chunks. The BIOS effectively rounds up to the next
    // multiple of 8 words.
    const uint32_t wordCount = registers[2] & 0x1FFFFF;

    bool fixedSrc = (registers[2] >> 24) & 1;

    // OGDK DEBUG: Trace CpuFastSet - Removed
    /*
    if ((dst & 0xFF000000) == 0x03000000 && (dst & 0x7FFF) < 0x2000) {
      // Log removed
    }
    */

    // Always 32-bit; CpuFastSet transfers in 8-word blocks.
    const uint32_t units = (wordCount + 7u) & ~7u;
    const uint32_t batchSize = 64; // Update PPU every 64 units
    const int perUnitCycles = 4;

    if (units == 0) {
      AdvanceHLECycles(kSwiOverheadCycles);
      cpsr = savedCpsr;
      break;
    }

    if (fixedSrc) {
      uint32_t fixedVal = memory.Read32(src);
      for (uint32_t i = 0; i < units; ++i) {
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          currentInstrThumb ? (uint32_t)currentOp16
                                            : currentOp32,
                          dst, fixedVal, registers[13]);
        memory.Write32(dst, fixedVal);
        dst += 4;

        // Periodically advance PPU/timers to allow VBlank/HBlank
        if ((i + 1) % batchSize == 0) {
          AdvanceHLECycles(perUnitCycles * (int)batchSize);
        }
      }
    } else {
      for (uint32_t i = 0; i < units; ++i) {
        uint32_t val = memory.Read32(src);
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          currentInstrThumb ? (uint32_t)currentOp16
                                            : currentOp32,
                          dst, val, registers[13]);
        memory.Write32(dst, val);
        dst += 4;
        src += 4;

        // Periodically advance PPU/timers to allow VBlank/HBlank
        if ((i + 1) % batchSize == 0) {
          AdvanceHLECycles(perUnitCycles * (int)batchSize);
        }
      }
    }

    // Advance remaining cycles for any partial batch
    uint32_t remaining = units % batchSize;
    if (remaining > 0) {
      AdvanceHLECycles(perUnitCycles * (int)remaining);
    }
    // Advance SWI overhead
    AdvanceHLECycles(kSwiOverheadCycles);

    cpsr = savedCpsr;
    break;
  }
  case 0x0D: // GetBiosChecksum
    // Returns a constant checksum of the GBA BIOS ROM.
    // Many toolchains/documentation refer to this as SWI 0x0D.
    registers[0] = 0xBAAE187F;
    break;
  case 0x0E: // BgAffineSet
  {
    // R0 = Source Address, R1 = Destination Address, R2 = Number of
    // calculations Source: 4 bytes OrigCenterX, 4 bytes OrigCenterY, 2 bytes
    // DisplayCenterX, 2 bytes DisplayCenterY
    //         2 bytes ScaleX, 2 bytes ScaleY, 2 bytes Angle
    // Destination: 2 bytes PA, 2 bytes PB, 2 bytes PC, 2 bytes PD, 4 bytes
    // StartX, 4 bytes StartY
    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    uint32_t count = registers[2];

    for (uint32_t i = 0; i < count; ++i) {
      // Read source parameters
      int32_t origCenterX = (int32_t)memory.Read32(src);     // 8.8 fixed point
      int32_t origCenterY = (int32_t)memory.Read32(src + 4); // 8.8 fixed point
      int16_t dispCenterX = (int16_t)memory.Read16(src + 8);
      int16_t dispCenterY = (int16_t)memory.Read16(src + 10);
      int16_t scaleX = (int16_t)memory.Read16(src + 12); // 8.8 fixed point
      int16_t scaleY = (int16_t)memory.Read16(src + 14); // 8.8 fixed point
      uint16_t angle = memory.Read16(src + 16);          // 0-FFFF = 0-360°

      // Convert angle to radians (0-FFFF maps to 0-2π)
      double theta = (angle / 65536.0) * 2.0 * 3.14159265358979323846;
      double cosA = cos(theta);
      double sinA = sin(theta);

      // Calculate affine parameters (8.8 fixed point)
      // PA = cos / scaleX, PB = sin / scaleX, PC = -sin / scaleY, PD = cos /
      // scaleY
      int16_t pa = (int16_t)((cosA * 256.0) / (scaleX / 256.0));
      int16_t pb = (int16_t)((sinA * 256.0) / (scaleX / 256.0));
      int16_t pc = (int16_t)((-sinA * 256.0) / (scaleY / 256.0));
      int16_t pd = (int16_t)((cosA * 256.0) / (scaleY / 256.0));

      // Calculate start position (19.8 fixed point)
      // StartX = OrigCenterX - (PA * DispCenterX + PB * DispCenterY)
      // StartY = OrigCenterY - (PC * DispCenterX + PD * DispCenterY)
      int32_t startX =
          origCenterX - ((pa * dispCenterX + pb * dispCenterY) >> 8);
      int32_t startY =
          origCenterY - ((pc * dispCenterX + pd * dispCenterY) >> 8);

      // Write destination
      memory.Write16(dst, (uint16_t)pa);
      memory.Write16(dst + 2, (uint16_t)pb);
      memory.Write16(dst + 4, (uint16_t)pc);
      memory.Write16(dst + 6, (uint16_t)pd);
      TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                        currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                        dst + 8, (uint32_t)startX, registers[13]);
      memory.Write32(dst + 8, (uint32_t)startX);
      TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                        currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                        dst + 12, (uint32_t)startY, registers[13]);
      memory.Write32(dst + 12, (uint32_t)startY);

      src += 20; // Source is 20 bytes
      dst += 16; // Destination is 16 bytes
    }
    break;
  }
  case 0x0F: // ObjAffineSet
  {
    // R0 = Source Address, R1 = Destination Address, R2 = Number of
    // calculations, R3 = Offset Source: 2 bytes ScaleX, 2 bytes ScaleY, 2 bytes
    // Angle (each entry is 8 bytes, padded) Destination: 2 bytes PA, 2 bytes
    // PB, 2 bytes PC, 2 bytes PD (written with R3 offset between each)
    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    uint32_t count = registers[2];
    uint32_t offset = registers[3];

    for (uint32_t i = 0; i < count; ++i) {
      // Read source parameters (8.8 fixed point)
      int16_t scaleX =
          (int16_t)memory.Read16(src); // 8.8 fixed point, 0x100 = 1.0
      int16_t scaleY = (int16_t)memory.Read16(src + 2); // 8.8 fixed point
      uint16_t angle = memory.Read16(src + 4);          // 0-FFFF = 0-360°

      // Convert angle to radians (0-FFFF maps to 0-2π)
      double theta = (angle / 65536.0) * 2.0 * 3.14159265358979323846;
      double cosA = cos(theta);
      double sinA = sin(theta);

      // Calculate affine parameters
      // For ObjAffineSet, the formula is different from BgAffineSet:
      // PA = cos * scaleX / 256, PB = sin * scaleX / 256
      // PC = -sin * scaleY / 256, PD = cos * scaleY / 256
      // This is because we're scaling UP (the inverse of background scaling)
      int16_t pa =
          (int16_t)(cosA * scaleX / 256.0 * 256.0); // Result is 8.8 fixed
      int16_t pb = (int16_t)(sinA * scaleX / 256.0 * 256.0);
      int16_t pc = (int16_t)(-sinA * scaleY / 256.0 * 256.0);
      int16_t pd = (int16_t)(cosA * scaleY / 256.0 * 256.0);

      // Write destination with offset
      memory.Write16(dst, (uint16_t)pa);
      memory.Write16(dst + offset, (uint16_t)pb);
      memory.Write16(dst + offset * 2, (uint16_t)pc);
      memory.Write16(dst + offset * 3, (uint16_t)pd);

      src += 8;          // Source entries are 8 bytes apart
      dst += offset * 4; // Move to next destination entry
    }
    break;
  }
  case 0x10: // BitUnPack
  {
    // R0 = Source, R1 = Dest, R2 = Pointer to UnPackInfo
    // UnPackInfo: 2 bytes SrcLen, 1 byte SrcWidth, 1 byte DestWidth, 4 bytes
    // DataOffset
    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    uint32_t info = registers[2];

    uint16_t srcLen = memory.Read16(info);
    uint8_t srcWidth = memory.Read8(info + 2);
    uint8_t dstWidth = memory.Read8(info + 3);
    uint32_t dataOffset = memory.Read32(info + 4);

    uint32_t srcMask = (1 << srcWidth) - 1;
    uint32_t dstBuffer = 0;
    int dstBitPos = 0;
    int srcBitPos = 0;
    uint8_t srcByte = 0;
    uint32_t bytesRead = 0;

    while (true) {
      // Read next byte if needed
      if (srcBitPos == 0) {
        if (bytesRead >= srcLen)
          break; // All bytes processed
        srcByte = memory.Read8(src++);
        bytesRead++;
      }

      uint32_t val = (srcByte >> srcBitPos) & srcMask;
      srcBitPos += srcWidth;
      if (srcBitPos >= 8) {
        srcBitPos = 0;
      }

      // Apply data offset if value is non-zero or zero-data flag set
      if (val != 0 || (dataOffset & 0x80000000)) {
        val += (dataOffset & 0x7FFFFFFF);
      }

      dstBuffer |= (val << dstBitPos);
      dstBitPos += dstWidth;

      if (dstBitPos >= 32) {
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          currentInstrThumb ? (uint32_t)currentOp16
                                            : currentOp32,
                          dst, dstBuffer, registers[13]);
        memory.Write32(dst, dstBuffer);
        dst += 4;
        dstBuffer = 0;
        dstBitPos = 0;
      }
    }

    // Write any remaining bits
    if (dstBitPos > 0) {
      TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                        currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                        dst, dstBuffer, registers[13]);
      memory.Write32(dst, dstBuffer);
    }
    break;
  }
  case 0x11: // LZ77UnCompWram - Decompress LZ77 to WRAM (8-bit writes)
  case 0x12: // LZ77UnCompVram - Decompress LZ77 to VRAM (16-bit writes)
  {
    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    bool toVram = (comment == 0x12);
    uint32_t origDst = dst;

    // Read header: bits 4-7 = compression type (1 = LZ77), bits 8-31 =
    // decompressed size
    uint32_t header = memory.Read32(src);
    uint32_t decompSize = header >> 8;

    // OG-DK: Trace LZ77 to IWRAM 0x03007400 or VRAM tilemaps
    const bool traceOgdk = false; // (origDst == 0x03007400u);
    const bool traceVramTilemap = false;
    // (origDst == 0x06006800u || origDst == 0x06003200u);
    if (traceOgdk || traceVramTilemap) {
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "OGDK_LZ77",
          "LZ77 decompress src=0x%08x dst=0x%08x size=%u", src, origDst,
          decompSize);
    }

    // Debug: trace LZ77 decompression - especially palette writes
    if ((dst & 0xFF000000) == 0x05000000) {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[SWI 0x12] LZ77 PALETTE: src=0x%x dst=0x%x size=0x%x", src, dst,
               decompSize);
      AIO::Emulator::Common::Logger::Instance().Log(
          AIO::Emulator::Common::LogLevel::Info, "BIOS", buf);
    }
    src += 4;

    uint32_t written = 0;
    uint16_t vramBuffer = 0;
    bool vramBufferFull = false;

    while (written < decompSize) {
      uint8_t flags = memory.Read8(src++);

      for (int i = 7; i >= 0 && written < decompSize; --i) {
        if (flags & (1 << i)) {
          // Compressed block
          uint8_t byte1 = memory.Read8(src++);
          uint8_t byte2 = memory.Read8(src++);
          uint32_t length = ((byte1 >> 4) & 0xF) + 3;
          uint32_t offset = ((byte1 & 0xF) << 8) | byte2;
          offset += 1;

          for (uint32_t j = 0; j < length && written < decompSize; ++j) {
            uint8_t val = memory.Read8(dst - offset);
            if (toVram) {
              if (!vramBufferFull) {
                vramBuffer = val;
                vramBufferFull = true;
              } else {
                vramBuffer |= (val << 8);
                memory.Write16(dst & ~1, vramBuffer);
                vramBufferFull = false;
              }
            } else {
              memory.Write8(dst, val);
            }
            dst++;
            written++;
          }
        } else {
          // Uncompressed byte
          uint8_t val = memory.Read8(src++);
          if (toVram) {
            if (!vramBufferFull) {
              vramBuffer = val;
              vramBufferFull = true;
            } else {
              vramBuffer |= (val << 8);
              memory.Write16(dst & ~1, vramBuffer);
              vramBufferFull = false;
            }
          } else {
            memory.Write8(dst, val);
          }
          dst++;
          written++;
        }
      }
    }

    // CRITICAL BUG FIX: Flush remaining vramBuffer byte if decompSize is odd
    // After decompression loop ends, if vramBufferFull is true, there's an
    // unflushed byte in vramBuffer that must be written
    if (toVram && vramBufferFull) {
      memory.Write16((dst - 1) & ~1, vramBuffer);
    }

    // OG-DK: Dump decompressed data around palette buffer offset
    if (traceOgdk || traceVramTilemap) {
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "OGDK_LZ77",
          "LZ77 done, written=%u bytes. Dumping first 32 bytes:", written);
      for (int i = 0; i < 32; i += 4) {
        uint32_t w = memory.Read32(origDst + i);
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "OGDK_LZ77",
            "  [0x%04x]: 0x%08x", i, w);
      }
      AIO::Emulator::Common::Logger::Instance().Log(
          AIO::Emulator::Common::LogLevel::Info, "OGDK_LZ77",
          "Dumping data at offset 0x10c (palette buffer, 8 bytes valid):");
      for (int i = 0; i < 32; i += 4) {
        uint32_t w = memory.Read32(origDst + 0x10c + i);
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "OGDK_LZ77",
            "  [0x%04x]: 0x%08x", 0x10c + i, w);
      }
    }

    break;
  }
  case 0x13: // HuffUnComp - Huffman decompression (based on mGBA)
  {
    // GBA BIOS Huffman Decompression (SWI 0x13)
    // Exact port from mGBA src/gba/bios.c _unHuffman()

    uint32_t source = registers[0] & 0xFFFFFFFC; // Align to 4 bytes
    uint32_t dest = registers[1];

    // Read header (4 bytes)
    uint32_t header = memory.Read32(source);
    int remaining = header >> 8;  // Decompressed size
    unsigned bits = header & 0xF; // 4 or 8 bits per symbol

    // Debug: trace Huffman decompression - especially palette writes
    if ((dest & 0xFF000000) == 0x05000000) {
      char buf[256];
      snprintf(
          buf, sizeof(buf),
          "[SWI 0x13] HUFFMAN PALETTE: src=0x%x dst=0x%x size=0x%x bits=%u",
          source, dest, remaining, bits);
      AIO::Emulator::Common::Logger::Instance().Log(
          AIO::Emulator::Common::LogLevel::Info, "BIOS", buf);
    }

    if (bits == 0) {
      bits = 8; // mGBA defaults to 8 if 0
    }
    if (32 % bits || bits == 1) {
      // Unaligned Huffman not supported
      break;
    }

    // Tree size: (size_byte << 1) + 1 = actual tree table size in bytes
    int treesize = (memory.Read8(source + 4) << 1) + 1;

    // Tree base is at source + 5 (after header + tree size byte)
    uint32_t treeBase = source + 5;

    // Bitstream starts after tree table
    uint32_t bitSource = source + 5 + treesize;

    // Current node pointer, starts at root
    uint32_t nPointer = treeBase;

    // Read root node data
    uint8_t node = memory.Read8(nPointer);

    int block = 0;
    int bitsSeen = 0;

    while (remaining > 0) {
      // Load next 32-bit word of compressed bitstream
      uint32_t bitstream = memory.Read32(bitSource);
      bitSource += 4;

      // Process all 32 bits
      for (int bitsRemaining = 32; bitsRemaining > 0 && remaining > 0;
           --bitsRemaining, bitstream <<= 1) {
        // Calculate next child address
        // Offset field is bits 0-5 of node
        uint32_t offset = node & 0x3F;
        uint32_t next = (nPointer & ~1u) + offset * 2 + 2;

        int readBits;

        if (bitstream & 0x80000000) {
          // Bit is 1 - go right (Node1)
          // RTerm is bit 6 - if set, right child is data
          if (node & 0x40) {
            // Terminal node - read data
            readBits = memory.Read8(next + 1);
          } else {
            // Non-terminal - continue traversal
            nPointer = next + 1;
            node = memory.Read8(nPointer);
            continue;
          }
        } else {
          // Bit is 0 - go left (Node0)
          // LTerm is bit 7 - if set, left child is data
          if (node & 0x80) {
            // Terminal node - read data
            readBits = memory.Read8(next);
          } else {
            // Non-terminal - continue traversal
            nPointer = next;
            node = memory.Read8(nPointer);
            continue;
          }
        }

        // Accumulate decoded bits into output block
        // Mask to only use 'bits' bits (4 or 8)
        block |= (readBits & ((1 << bits) - 1)) << bitsSeen;
        bitsSeen += bits;

        // Reset to root for next symbol
        nPointer = treeBase;
        node = memory.Read8(nPointer);

        // Write when we have 32 bits
        if (bitsSeen == 32) {
          bitsSeen = 0;
          TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                            currentInstrThumb ? (uint32_t)currentOp16
                                              : currentOp32,
                            dest, block, registers[13]);
          memory.Write32(dest, block);
          dest += 4;
          remaining -= 4;
          block = 0;
        }
      }
    }

    // CRITICAL BUG FIX: Flush remaining block data if decompressed size is not
    // a multiple of 4. If bitsSeen > 0, there are unflushed bits in 'block'
    // that haven't been written. This causes data loss in non-4-byte-aligned
    // decompressed output, leading to palette/tile corruption.
    if (bitsSeen > 0 && remaining <= 0) {
      // Only write the bytes we actually need (not full 32-bit block)
      for (int b = 0; b < (bitsSeen + 7) / 8; ++b) {
        uint8_t byteVal = (block >> (b * 8)) & 0xFF;
        memory.Write8Internal(dest + b, byteVal);
      }
    }

    // Update registers like real BIOS
    registers[0] = bitSource;
    registers[1] = dest;

    break;
  }
  case 0x14: // RLUnCompWram - Run-Length decompression to WRAM
  case 0x15: // RLUnCompVram - Run-Length decompression to VRAM
  {
    uint32_t src = registers[0];
    uint32_t dst = registers[1];
    bool toVram = (comment == 0x15);

    uint32_t header = memory.Read32(src);
    uint32_t decompSize = header >> 8;

    // Debug: trace RLE decompression - especially palette writes
    if ((dst & 0xFF000000) == 0x05000000) {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "[SWI 0x15] RLE PALETTE: src=0x%x dst=0x%x size=0x%x", src, dst,
               decompSize);
      AIO::Emulator::Common::Logger::Instance().Log(
          AIO::Emulator::Common::LogLevel::Info, "BIOS", buf);
    }
    src += 4;

    uint32_t written = 0;
    uint16_t vramBuffer = 0;
    bool vramBufferFull = false;

    while (written < decompSize) {
      uint8_t flag = memory.Read8(src++);

      if (flag & 0x80) {
        // Compressed run
        uint32_t length = (flag & 0x7F) + 3;
        uint8_t val = memory.Read8(src++);

        for (uint32_t i = 0; i < length && written < decompSize; ++i) {
          if (toVram) {
            if (!vramBufferFull) {
              vramBuffer = val;
              vramBufferFull = true;
            } else {
              vramBuffer |= (val << 8);
              memory.Write16(dst & ~1, vramBuffer);
              vramBufferFull = false;
            }
          } else {
            memory.Write8(dst, val);
          }
          dst++;
          written++;
        }
      } else {
        // Uncompressed run
        uint32_t length = (flag & 0x7F) + 1;

        for (uint32_t i = 0; i < length && written < decompSize; ++i) {
          uint8_t val = memory.Read8(src++);
          if (toVram) {
            if (!vramBufferFull) {
              vramBuffer = val;
              vramBufferFull = true;
            } else {
              vramBuffer |= (val << 8);
              memory.Write16(dst & ~1, vramBuffer);
              vramBufferFull = false;
            }
          } else {
            memory.Write8(dst, val);
          }
          dst++;
          written++;
        }
      }
    }

    // CRITICAL BUG FIX: Flush remaining vramBuffer byte if decompSize is odd
    // After decompression loop ends, if vramBufferFull is true, there's an
    // unflushed byte in vramBuffer that must be written
    if (toVram && vramBufferFull) {
      memory.Write16((dst - 1) & ~1, vramBuffer);
    }

    break;
  }
  case 0x16: // Diff8bitUnFilterWram
  case 0x17: // Diff8bitUnFilterVram
  case 0x18: // Diff16bitUnFilter
  {
    // Differential unfilter - used less commonly
    uint32_t src = registers[0];
    uint32_t dst = registers[1];

    uint32_t header = memory.Read32(src);
    uint32_t size = header >> 8;
    src += 4;

    if (comment == 0x18) {
      // 16-bit differential
      uint16_t prev = 0;
      for (uint32_t i = 0; i < size; i += 2) {
        uint16_t diff = memory.Read16(src);
        src += 2;
        prev += diff;
        memory.Write16(dst, prev);
        dst += 2;
      }
    } else {
      // 8-bit differential
      uint8_t prev = 0;
      for (uint32_t i = 0; i < size; ++i) {
        uint8_t diff = memory.Read8(src++);
        prev += diff;
        memory.Write8(dst++, prev);
      }
    }
    break;
  }
  case 0x19: // SoundBias - Set sound bias
  {
    // R0 = delay, bias level
    // Not critical for most games
    break;
  }
  case 0x1F: // MidiKey2Freq - MIDI to frequency conversion
  {
    // Used for sound - not critical for gameplay
    // R0 = WaveData pointer, R1 = MIDI key, R2 = Fine adjust
    // Returns frequency in R0
    uint32_t key = registers[1];
    uint32_t fine = registers[2];
    // Approximate conversion
    double freq = 440.0 * pow(2.0, (key - 69 + fine / 256.0) / 12.0);
    registers[0] = (uint32_t)(freq * 2048.0); // Fixed-point result
    break;
  }
  default:
    std::cout << "Unimplemented SWI 0x" << std::hex << comment
              << " at PC=" << registers[15] << std::endl;
    break;
  }

  // Real BIOS returns with "MOV R2, #4" (0xE3A02004) at the SWI return
  // point. Reads from BIOS region outside BIOS code return this prefetched
  // value (open-bus behavior).
  memory.SetBiosPrefetch(0xE3A02004);
}

void ARM7TDMI::AdvanceHLECycles(int cycles) {
  if (cycles <= 0)
    return;
  hleCyclesThisStep += cycles;
  memory.AdvanceCycles(cycles);
}

int ARM7TDMI::ConsumeHLECycles() {
  const int cycles = hleCyclesThisStep;
  hleCyclesThisStep = 0;
  return cycles;
}

void ARM7TDMI::DecodeThumb(uint16_t instruction, uint32_t pcValue) {
  // Thumb Instruction Decoding

  // DEBUG: Trace EEPROM validation loop at 0x809e1cc
  // static int eepromLoopCount = 0;
  // if (registers[15] >= 0x0809E1CC && registers[15] <= 0x0809E1F0) {
  //     if (eepromLoopCount++ % 100 == 0) { // Log every 100 iterations
  //         std::cerr << "[EEPROM LOOP #" << eepromLoopCount << "] PC=0x" <<
  //         std::hex << registers[15]
  //                   << " instr=0x" << instruction
  //                   << " R0=0x" << registers[0] << " R1=0x" << registers[1]
  //                   << " R2=0x" << registers[2] << " R3=0x" << registers[3]
  //                   << std::dec << std::endl;
  //     }
  // }

  // Trace VBlank handler button processing - DISABLED for clean output
  /*
  uint32_t execPC = registers[15] - 2;

  // Trace only first few button events
  if (execPC >= 0x80014c2 && execPC <= 0x80014e0) {
      // Check if this is button input processing with actual buttons
      if (registers[0] != 0x3ff && registers[0] != 0xfffffc00 &&
          registers[0] != 0xfc000000 && registers[0] != 0xfc00 &&
          registers[0] != 0 && registers[0] != 0xffffffff) {
          static int btnTraceCount = 0;
          if (btnTraceCount++ < 20) {
              std::cout << "[BTN TRACE] PC=0x" << std::hex << execPC
                        << " Instr=0x" << instruction
                        << " R0=0x" << registers[0]
                        << " R1=0x" << registers[1]
                        << " R2=0x" << registers[2]
                        << " R3=0x" << registers[3]
                        << " R4=0x" << registers[4] << std::dec << std::endl;
          }
      }
  }
  */

  // Trace main loop - DISABLED
  /*
  uint32_t execPC = registers[15] - 2;
  if (execPC >= 0x08000510 && execPC <= 0x08000550) {
       std::cout << "[MAIN LOOP] PC=0x" << std::hex << execPC << " Instr=0x" <<
  instruction
                 << " R0=" << registers[0] << " R1=" << registers[1] << " R2="
  << registers[2]
                 << " CPSR=" << cpsr << std::dec << std::endl;
  }
  */

  // Format 2: Add/Subtract
  // 0001 1xxx xxxx xxxx
  if ((instruction & 0xF800) == 0x1800) {
    bool I = (instruction >> 10) & 1;
    bool sub = (instruction >> 9) & 1;
    uint32_t rn = (instruction >> 6) & 0x7; // If I=0, Rn. If I=1, Imm3
    uint32_t rs = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t op2 = I ? rn : registers[rn];
    uint32_t val = registers[rs];
    uint32_t res = 0;

    if (sub) {
      res = val - op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(val, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(val, op2, res));
    } else {
      res = val + op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, DetectAddCarry(val, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectAddOverflow(val, op2, res));
    }

    registers[rd] = res;

    if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
      // std::cout << "ADD/SUB: Rd=R" << rd << " Val=0x" << std::hex << res <<
      // std::endl;
    }
  }
  // Format 1: Move Shifted Register
  // 000x xxxx xxxx xxxx
  else if ((instruction & 0xE000) == 0x0000) {
    uint32_t opcode = (instruction >> 11) & 0x3;
    uint32_t offset = (instruction >> 6) & 0x1F;
    uint32_t rs = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t val = registers[rs];
    uint32_t res = 0;
    uint32_t tempCPSR = cpsr;
    if (opcode == 0) { // LSL
      res = LogicalShiftLeft(val, offset, tempCPSR, true);
    } else if (opcode == 1) { // LSR
      res = LogicalShiftRight(val, offset == 0 ? 32 : offset, tempCPSR, true);
    } else if (opcode == 2) { // ASR
      res =
          ArithmeticShiftRight(val, offset == 0 ? 32 : offset, tempCPSR, true);
    }

    // Debug shift operations at VBlank handler input processing - DISABLED
    /*
    if (registers[15] >= 0x80014c4 && registers[15] <= 0x80014ca) {
        static int shiftLogCount = 0;
        if (shiftLogCount++ < 200 || (val != 0xfffffc00 && val != 0xfc000000)) {
            std::cout << "[SHIFT at VBlankHandler] PC=0x" << std::hex <<
    registers[15]
                      << " opcode=" << std::dec << opcode << " offset=" <<
    offset
                      << " val=0x" << std::hex << val << " res=0x" << res <<
    std::dec << std::endl;
        }
    }
    */

    UpdateNZFlags(cpsr, res);
    SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
    registers[rd] = res;

    if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
      // std::cout << "Shift: Rd=R" << rd << " Val=0x" << std::hex << res <<
      // std::endl;
    }
  }
  // Format 3: Move/Compare/Add/Subtract Immediate
  // 001x xxxx xxxx xxxx
  else if ((instruction & 0xE000) == 0x2000) {
    uint32_t opcode = (instruction >> 11) & 0x3;
    uint32_t rd = (instruction >> 8) & 0x7;
    uint32_t imm = instruction & 0xFF;

    if (opcode == 0) { // MOV Rd, #Offset8
      registers[rd] = imm;
      UpdateNZFlags(cpsr, registers[rd]);
    } else if (opcode == 1) { // CMP Rd, #Offset8
      uint32_t result = registers[rd] - imm;
      UpdateNZFlags(cpsr, result);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(registers[rd], imm));
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectSubOverflow(registers[rd], imm, result));
    } else if (opcode == 2) { // ADD Rd, #Offset8
      uint32_t val = registers[rd];
      registers[rd] = val + imm;
      UpdateNZFlags(cpsr, registers[rd]);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, DetectAddCarry(val, imm));
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectAddOverflow(val, imm, registers[rd]));
    } else if (opcode == 3) { // SUB Rd, #Offset8
      uint32_t val = registers[rd];
      registers[rd] = val - imm;
      UpdateNZFlags(cpsr, registers[rd]);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(val, imm));
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectSubOverflow(val, imm, registers[rd]));
    }
  }
  // Format 4: ALU Operations
  // 0100 00xx xxxx xxxx
  else if ((instruction & 0xFC00) == 0x4000) {
    uint32_t opcode = (instruction >> 6) & 0xF;
    uint32_t rs = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t val = registers[rd];
    uint32_t op2 = registers[rs];
    uint32_t res = 0;

    switch (opcode) {
    case 0x0: // AND
      res = val & op2;
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    case 0x1: // EOR
      res = val ^ op2;
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    case 0x2: // LSL
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = LogicalShiftLeft(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      registers[rd] = res;
      break;
    }
    case 0x3: // LSR
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = LogicalShiftRight(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      registers[rd] = res;
      break;
    }
    case 0x4: // ASR
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = ArithmeticShiftRight(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      registers[rd] = res;
      break;
    }
    case 0x5: // ADC
    {
      bool carryIn = CarryFlagSet(cpsr);
      uint64_t result64 = static_cast<uint64_t>(val) +
                          static_cast<uint64_t>(op2) +
                          static_cast<uint64_t>(carryIn);
      res = static_cast<uint32_t>(result64);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, result64 > 0xFFFFFFFFULL);
      // V flag: signed overflow if both inputs had same sign but result differs
      // Must consider val+op2 first, then +carry, to avoid uint32 wraparound
      SetCPSRFlag(cpsr, CPSR::FLAG_V, ((val ^ res) & (op2 ^ res)) >> 31);
      registers[rd] = res;
      break;
    }
    case 0x6: // SBC
    {
      bool carryIn = CarryFlagSet(cpsr);
      res = val - op2 - static_cast<uint32_t>(!carryIn);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C,
                  val >= static_cast<uint64_t>(op2) + !carryIn);
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(val, op2, res));
      registers[rd] = res;
      break;
    }
    case 0x7: // ROR
    {
      // Thumb ALU ROR: shift amount is taken from Rs[7:0].
      // If amount==0: result unchanged, carry unchanged.
      // If amount!=0 and (amount&31)==0: result unchanged, carry = bit31.
      const uint32_t amount8 = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      if (amount8 == 0) {
        res = val;
      } else {
        const uint32_t rot = amount8 & 0x1F;
        if (rot == 0) {
          res = val;
          SetCPSRFlag(tempCPSR, CPSR::FLAG_C, (val & 0x80000000U) != 0);
        } else {
          res = RotateRight(val, rot, tempCPSR, true);
        }
      }
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      registers[rd] = res;
      break;
    }
    case 0x8: // TST
      res = val & op2;
      UpdateNZFlags(cpsr, res);
      break;
    case 0x9: // NEG (RSB Rd, Rs, #0)
      res = 0 - op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(0u, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(0u, op2, res));
      registers[rd] = res;
      break;
    case 0xA: // CMP
      res = val - op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(val, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(val, op2, res));
      break;
    case 0xB: // CMN
      res = val + op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, DetectAddCarry(val, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectAddOverflow(val, op2, res));
      break;
    case 0xC: // ORR
      res = val | op2;
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    case 0xD: // MUL
      res = val * op2;
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    case 0xE: // BIC
      res = val & (~op2);
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    case 0xF: // MVN
      res = ~op2;
      UpdateNZFlags(cpsr, res);
      registers[rd] = res;
      break;
    }
  }
  // Format 5: HiReg Operations / BX
  // 0100 01xx xxxx xxxx
  else if ((instruction & 0xFC00) == 0x4400) {
    // Thumb HiReg operations / BX encoding (Format 5):
    // 010001 op(2) H1 H2 Rs(3) Rd(3)
    // op is bits 9-8.
    uint32_t opcode = (instruction >> 8) & 0x3;
    bool h1 = (instruction >> 7) & 1;
    bool h2 = (instruction >> 6) & 1;
    uint32_t rm = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t regD = rd | (h1 << 3);
    uint32_t regM = rm | (h2 << 3);

    if (opcode == 0) { // ADD Rd, Rm
      uint32_t rmVal = (regM == 15) ? pcValue : registers[regM];
      if (regD == 15) {
        // ARM7TDMI: when PC is read as Rd, it returns instrAddr+4 (pcValue)
        uint32_t newPC = pcValue + rmVal;
        LogBranch(registers[15] - 2, newPC);
        // In Thumb, ADD to PC should clear bit 0 (PC must be aligned)
        registers[15] = newPC & 0xFFFFFFFE;
        FlushPipeline(); // Thumb ADD PC invalidates prefetch
      } else {
        // Normal ADD Rd, Rm where Rd is not PC
        const uint32_t old = registers[regD];
        registers[regD] += rmVal;
        if (regD == 8) {
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[regD]);
        }
      }
      // Format 5 ADD does NOT set flags
    } else if (opcode == 1) { // CMP Rd, Rm
      // PC as source needs +2 adjustment in Thumb mode
      uint32_t val = (regD == 15) ? pcValue : registers[regD];
      uint32_t op2 = (regM == 15) ? pcValue : registers[regM];
      uint32_t res = val - op2;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(val, op2));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(val, op2, res));
    } else if (opcode == 2) { // MOV Rd, Rm
      // Note: For HiReg operations, Rm is regM (includes h2).
      // A common bug is to accidentally use the low-register `rm` only.
      // Hi-reg MOV uses regM/regD (including h2/h1). Do not treat regM!=rm as
      // suspicious.
      if (regD == 15) {
        // PC as source needs +2 adjustment in Thumb mode
        const uint32_t val = (regM == 15) ? pcValue : registers[regM];
        LogBranch(registers[15] - 2, val);
        // In Thumb, MOV PC, Rm should clear bit 0 (not interworking on
        // ARM7TDMI)
        registers[15] = val & 0xFFFFFFFE;
        FlushPipeline(); // Thumb MOV PC invalidates prefetch
      } else {
        // PC as source needs +2 adjustment in Thumb mode
        const uint32_t val = (regM == 15) ? pcValue : registers[regM];
        const uint32_t old = registers[regD];
        registers[regD] = val;

        if (kEnableHeavyCpuTraces && (instruction & 0xFF00) == 0x4600 &&
            regD == 8) {
          Logger::Instance().LogFmt(
              LogLevel::Error, "CPU",
              "THUMB HiReg MOV into R8: fromPC=0x%08x instr=0x%04x h1=%u h2=%u "
              "rm=%u rd=%u regM=%u val=0x%08x (R11=0x%08x) old=0x%08x "
              "new=0x%08x",
              currentInstrAddr, instruction, (unsigned)h1, (unsigned)h2,
              (unsigned)rm, (unsigned)rd, (unsigned)regM, val, registers[11],
              old, registers[regD]);
        }

        if (regD == 8) {
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[regD]);
        }
        if (regD == 11) {
          const uint32_t tracedInstr = (uint32_t)currentOp16;
          TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                        registers[regD]);
        }
      }
    } else if (opcode == 3) { // BX Rm
      uint32_t target = registers[regM];

      // Focused debug: detect unexpected interworking near the early SMA2 crash
      // region.
      if (kEnableHeavyCpuTraces &&
          ((currentInstrAddr >= 0x08001600 && currentInstrAddr <= 0x08001820) ||
           ((target & 0xFFFFFFFEu) >= 0x08001600 &&
            (target & 0xFFFFFFFEu) <= 0x08001820) ||
           ((target & 0xFFFFFFFEu) == 0x08001774))) {
        Logger::Instance().LogFmt(
            LogLevel::Error, "CPU",
            "THUMB BX at PC=0x%08x: Rm=%u target=0x%08x -> %s",
            currentInstrAddr, (unsigned)regM, target,
            (target & 1) ? "Thumb" : "ARM");
      }

      LogBranch(registers[15] - 2, target);
      if (target & 1) {
        thumbMode = true;
        cpsr |= 0x20; // Set T bit in CPSR
        registers[15] = target & 0xFFFFFFFE;
      } else {
        thumbMode = false;
        cpsr &= ~0x20; // Clear T bit in CPSR
        registers[15] = target & 0xFFFFFFFC;
      }
      FlushPipeline(); // Thumb BX invalidates prefetch (and may switch mode)
    }
  }
  // Format 6: PC-relative Load
  // 0100 1xxx xxxx xxxx
  else if ((instruction & 0xF800) == 0x4800) {
    uint32_t rd = (instruction >> 8) & 0x7;
    uint32_t imm = instruction & 0xFF;
    uint32_t addr = (pcValue & 0xFFFFFFFC) + (imm * 4);
    registers[rd] = memory.Read32(addr);

    if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
      // std::cout << "LDR PC-Rel: Rd=R" << rd << " Addr=0x" << std::hex << addr
      // << " Val=0x" << registers[rd] << std::endl;
    }
  }
  // Format 7: Load/Store with Register Offset
  // 0101 L B 0 Ro Rb Rd
  // L=0: STR/STRB, L=1: LDR/LDRB
  // B=0: word, B=1: byte
  else if ((instruction & 0xF200) == 0x5000) {
    const bool L = ((instruction >> 11) & 1u) != 0u;
    const bool B = ((instruction >> 10) & 1u) != 0u;
    const uint32_t ro = (instruction >> 6) & 0x7u;
    const uint32_t rb = (instruction >> 3) & 0x7u;
    const uint32_t rd = instruction & 0x7u;

    const uint32_t addr = registers[rb] + registers[ro];
    if (L) {
      if (B) {
        registers[rd] = memory.Read8(addr);
      } else {
        // Thumb word loads share the same unaligned semantics as ARM LDR:
        // align address, then rotate right by 8 * (addr[1:0]).
        const uint32_t alignedAddr = addr & ~3u;
        uint32_t val = memory.Read32(alignedAddr);
        const uint32_t rotBytes = (addr & 3u) * 8u;
        if (rotBytes != 0) {
          val = (val >> rotBytes) | (val << (32u - rotBytes));
        }
        TraceWatchRead32(currentInstrAddr, true, (uint32_t)currentOp16, addr,
                         val, registers[13]);
        registers[rd] = val;
      }
    } else {
      if (B) {
        memory.Write8(addr, registers[rd] & 0xFFu);
      } else {
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          (uint32_t)currentOp16, addr, registers[rd],
                          registers[13]);
        memory.Write32(addr, registers[rd]);
      }
    }
  }
  // Format 8: Load/Store Sign-Extended Byte/Halfword
  // 0101 001x xxxx xxxx
  else if ((instruction & 0xF200) == 0x5200) {
    bool H = (instruction >> 11) & 1;
    bool S = (instruction >> 10) & 1; // Sign extended
    uint32_t ro = (instruction >> 6) & 0x7;
    uint32_t rb = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t addr = registers[rb] + registers[ro];

    if (S) {
      if (H) { // LDRSH
        // ARM7TDMI: LDRSH from odd address behaves as LDRSB
        if (addr & 1) {
          int8_t val = (int8_t)memory.Read8(addr);
          registers[rd] = (int32_t)val;
        } else {
          int16_t val = (int16_t)memory.Read16(addr);
          registers[rd] = (int32_t)val;
        }
      } else { // LDRSB
        int8_t val = (int8_t)memory.Read8(addr);
        registers[rd] = (int32_t)val;
      }
    } else {
      if (H) { // LDRH
        // ARM7TDMI: LDRH from odd address reads aligned halfword rotated by 8
        if (addr & 1) {
          uint32_t value = memory.Read16(addr & ~1u);
          registers[rd] = (value >> 8) | (value << 24);
        } else {
          registers[rd] = memory.Read16(addr);
        }
      } else { // STRH
        memory.Write16(addr, registers[rd] & 0xFFFF);
      }
    }
  }
  // Format 9: Load/Store with Immediate Offset
  // 011x xxxx xxxx xxxx (STRB/LDRB)
  else if ((instruction & 0xE000) == 0x6000) {
    bool B = (instruction >> 12) &
             1; // 0=STR, 1=LDR (Word) - Wait, this is Format 9?
    // Format 9: 011B L5 Rn Rd
    // B=0: STR/LDR Word (Format 9 is Byte/Word?)
    // GBATEK: Format 9: Load/Store with Immediate Offset
    // 011B L5 Rn Rd
    // B=0: Word, B=1: Byte
    // L=0: Store, L=1: Load

    bool L = (instruction >> 11) & 1;
    uint32_t imm = (instruction >> 6) & 0x1F;
    uint32_t rn = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t addr = registers[rn] + (imm * (B ? 1 : 4));

    // DEBUG: Trace ALL STR to 0x03xxxxxx range (disabled)
    // if (!L && !B && (addr >> 24) == 0x03) {
    //     static int strCount = 0;
    //     if (strCount++ < 100 || ((addr & 0x7FFF) >= 0x1500 && (addr & 0x7FFF)
    //     < 0x1600)) {
    //         std::cout << "[F9 STR] PC=0x" << std::hex << (registers[15] - 2)
    //                   << " instr=0x" << instruction
    //                   << " Addr=0x" << addr << " Val=0x" << registers[rd]
    //                   << " (R" << std::dec << rd << "=[R" << rn << "+#" <<
    //                   (imm*4) << "])" << std::endl;
    //     }
    // }

    if (L) { // Load
      if (B) {
        registers[rd] = memory.Read8(addr);
      } else {
        const uint32_t alignedAddr = addr & ~3u;
        uint32_t val = memory.Read32(alignedAddr);
        const uint32_t rotBytes = (addr & 3u) * 8u;
        if (rotBytes != 0) {
          val = (val >> rotBytes) | (val << (32u - rotBytes));
        }
        registers[rd] = val;
      }
    } else { // Store
      if (B)
        memory.Write8(addr, registers[rd] & 0xFF);
      else {
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          (uint32_t)currentOp16, addr, registers[rd],
                          registers[13]);
        memory.Write32(addr, registers[rd]);
      }
    }
  }
  // Format 10: Load/Store Halfword
  // 1000 xxxx xxxx xxxx
  else if ((instruction & 0xF000) == 0x8000) {
    bool L = (instruction >> 11) & 1;
    uint32_t imm = (instruction >> 6) & 0x1F;
    uint32_t rn = (instruction >> 3) & 0x7;
    uint32_t rd = instruction & 0x7;

    uint32_t addr = registers[rn] + (imm * 2);

    if (L) {
      // ARM7TDMI: LDRH from odd address reads aligned halfword rotated by 8
      if (addr & 1) {
        uint32_t value = memory.Read16(addr & ~1u);
        registers[rd] = (value >> 8) | (value << 24);
      } else {
        registers[rd] = memory.Read16(addr);
      }
    } else {
      // Debug STRH to button state memory at 0x3002b94 - DISABLED
      /*
      if (addr == 0x3002b94) {
          static int strhCount = 0;
          if (strhCount++ < 200 || (registers[rd] & 0xFFFF) != 0xfc00) {
              std::cout << "[STRH to button state] addr=0x" << std::hex << addr
                        << " rd=" << std::dec << rd << " val=0x" << std::hex <<
      (registers[rd] & 0xFFFF)
                        << " PC=0x" << registers[15] << std::dec << std::endl;
          }
      }
      */
      memory.Write16(addr, registers[rd] & 0xFFFF);
    }
  }
  // Format 12: Add Offset to PC / SP
  // 1010 xddd oooooooo
  // x=0: ADD Rd, PC, #imm*4   (PC is (pcValue & ~3))
  // x=1: ADD Rd, SP, #imm*4
  else if ((instruction & 0xF000) == 0xA000) {
    const bool useSP = ((instruction >> 11) & 1u) != 0;
    const uint32_t rd = (instruction >> 8) & 0x7u;
    const uint32_t imm8 = instruction & 0xFFu;
    const uint32_t offset = imm8 << 2u;

    const uint32_t base = useSP ? registers[13] : (pcValue & ~3u);
    registers[rd] = base + offset;
  }
  // Format 9: SP-relative Load/Store (CRITICAL - was missing!)
  // 1001 xxxx xxxx xxxx
  else if ((instruction & 0xF000) == 0x9000) {
    bool L = (instruction >> 11) & 1; // 0=STR, 1=LDR
    uint32_t rd = (instruction >> 8) & 0x7;
    uint32_t imm = instruction & 0xFF;

    uint32_t addr = registers[13] + (imm * 4); // SP + offset*4

    if (L) {
      // LDR Rd, [SP, #imm]
      registers[rd] = memory.Read32(addr);
    } else {
      // STR Rd, [SP, #imm]
      TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                        (uint32_t)currentOp16, addr, registers[rd],
                        registers[13]);
      memory.Write32(addr, registers[rd]);
    }
  }
  // Format 11: Load/Store Multiple
  // 1100 xxxx xxxx xxxx
  else if ((instruction & 0xF000) == 0xC000) {
    bool L = (instruction >> 11) & 1;
    uint32_t rb = (instruction >> 8) & 0x7;
    uint8_t rList = instruction & 0xFF;

    uint32_t addr = registers[rb];
    uint32_t startAddr = addr;

    for (int i = 0; i < 8; ++i) {
      if ((rList >> i) & 1) {
        if (L) { // Load
          registers[i] = memory.Read32(addr);
        } else { // Store
          // if (traceMixbuf) {
          //     std::cout << "  Store R" << i << "=0x" << std::hex <<
          //     registers[i]
          //               << " to 0x" << addr << std::dec << std::endl;
          // }
          TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                            currentInstrThumb ? (uint32_t)currentOp16
                                              : currentOp32,
                            addr, registers[i], registers[13]);
          memory.Write32(addr, registers[i]);
        }
        addr += 4;
      }
    }

    // Write-back (debug disabled)
    // ARM7TDMI: For LDMIA, if Rb is in the register list, loaded value wins
    if (L && ((rList >> rb) & 1)) {
      // Rb was loaded from memory — don't overwrite with writeback
    } else {
      registers[rb] = addr;
    }
  }
  // Format 13: Add Offset to Stack Pointer
  // 1011 0000 xxxx xxxx
  else if ((instruction & 0xFF00) == 0xB000) {
    bool S = (instruction >> 7) & 1;
    uint32_t imm = instruction & 0x7F;
    imm *= 4;

    uint32_t oldSP = registers[13];
    if (S)
      registers[13] = oldSP - imm; // SUB
    else
      registers[13] = oldSP + imm; // ADD

    if (registers[13] < 0x02000000 || registers[13] >= 0x04000000) {
      Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                "SP WARNING: Thumb Format13 (ADD/SUB SP,#imm): "
                                "fromPC=0x%08x oldSP=0x%08x newSP=0x%08x",
                                currentInstrAddr, oldSP, registers[13]);
    }
  }
  // Format 14: Push/Pop Registers
  // 1011 x10x xxxx xxxx
  else if ((instruction & 0xF600) == 0xB400) {
    bool L = (instruction >> 11) & 1; // 0=Push, 1=Pop
    bool R = (instruction >> 8) & 1;  // PC/LR
    uint8_t rList = instruction & 0xFF;

    uint32_t oldSP = registers[13];

    if (!L) { // PUSH
      // Decrement SP, Store Registers
      // R=1: Push LR
      // Rlist: Push R0-R7

      uint32_t sp = registers[13];
      uint32_t count = 0;
      if (R)
        count++;
      for (int i = 0; i < 8; ++i)
        if ((rList >> i) & 1)
          count++;

      sp -= (count * 4);
      uint32_t currentAddr = sp;
      registers[13] = sp;

      if (kEnableHeavyCpuTraces &&
          (currentInstrAddr == 0x08007320 || (rList == 0x18 && !R))) {
        Logger::Instance().LogFmt(
            LogLevel::Error, "CPU",
            "THUMB PUSH rList=0x%02x R=%u at PC=0x%08x: oldSP=0x%08x "
            "newSP=0x%08x r3=0x%08x r4=0x%08x",
            rList, (unsigned)R, currentInstrAddr, oldSP, sp, registers[3],
            registers[4]);
      }

      for (int i = 0; i < 8; ++i) {
        if ((rList >> i) & 1) {
          TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                            (uint32_t)currentOp16, currentAddr, registers[i],
                            registers[13]);
          memory.Write32(currentAddr, registers[i]);
          currentAddr += 4;
        }
      }
      if (R) {
        TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                          (uint32_t)currentOp16, currentAddr, registers[14],
                          registers[13]);
        memory.Write32(currentAddr, registers[14]); // Push LR
      }
    } else { // POP
      // Load Registers, Increment SP
      // R=1: Pop PC
      // Rlist: Pop R0-R7

      uint32_t sp = registers[13];
      uint32_t currentAddr = sp;

      if (kEnableHeavyCpuTraces &&
          (currentInstrAddr == 0x08007320 || (rList == 0x18 && !R))) {
        const uint32_t w0 = memory.Read32(sp);
        const uint32_t w1 = memory.Read32(sp + 4);
        Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                  "THUMB POP rList=0x%02x R=%u at PC=0x%08x: "
                                  "SP=0x%08x [SP]=0x%08x [SP+4]=0x%08x",
                                  rList, (unsigned)R, currentInstrAddr, sp, w0,
                                  w1);
      }

      for (int i = 0; i < 8; ++i) {
        if ((rList >> i) & 1) {
          const uint32_t val = memory.Read32(currentAddr);
          TraceWatchRead32(currentInstrAddr, true, (uint32_t)currentOp16,
                           currentAddr, val, registers[13]);
          registers[i] = val;
          currentAddr += 4;
        }
      }
      if (R) {
        uint32_t pc = memory.Read32(currentAddr);
        TraceWatchRead32(currentInstrAddr, true, (uint32_t)currentOp16,
                         currentAddr, pc, registers[13]);
        if (kEnableHeavyCpuTraces && pc >= 0x08125C00 && pc < 0x08127000) {
          Logger::Instance().LogFmt(
              LogLevel::Error, "CPU",
              "POP {PC} instruction: PC=0x%08x SP=0x%08x popped_pc=0x%08x",
              registers[15] - 2, currentAddr, pc);
        }
        LogBranch(registers[15] - 2, pc);
        registers[15] = pc & 0xFFFFFFFE; // Pop PC (Thumb)
        registers[15] &= ~1;
        FlushPipeline(); // POP PC invalidates prefetch
        currentAddr += 4;
      }

      registers[13] = currentAddr;

      if (kEnableHeavyCpuTraces &&
          (currentInstrAddr == 0x08007320 || (rList == 0x18 && !R))) {
        Logger::Instance().LogFmt(
            LogLevel::Error, "CPU",
            "THUMB POP done at PC=0x%08x: newSP=0x%08x r3=0x%08x r4=0x%08x",
            currentInstrAddr, registers[13], registers[3], registers[4]);
      }
    }

    if (kEnableHeavyCpuTraces &&
        (registers[13] < 0x02000000 || registers[13] >= 0x04000000)) {
      Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                "SP WARNING: Thumb Format14 (PUSH/POP): "
                                "fromPC=0x%08x oldSP=0x%08x newSP=0x%08x",
                                currentInstrAddr, oldSP, registers[13]);
    }
  }
  // Format 16: Conditional Branch
  // 1101 xxxx xxxx xxxx
  else if ((instruction & 0xF000) == 0xD000) {
    uint32_t cond = (instruction >> 8) & 0xF;
    int8_t offset = (int8_t)(instruction & 0xFF); // Signed 8-bit

    if (cond != 0xF) { // 0xF is SWI
      bool condSatisfied = CheckCondition(cond);

      if (condSatisfied) {
        // registers[15] points at the next instruction (instrAddr+2).
        // Branch base should be PC+4 (instrAddr+4) which equals
        // registers[15]+2.
        uint32_t target = registers[15] + 2 + (offset * 2);
        LogBranch(registers[15] - 2, target);
        registers[15] = target;
        FlushPipeline(); // Thumb conditional branch invalidates prefetch
      }
    } else {
      // SWI (Format 17)
      ExecuteSWI(instruction & 0xFF);
    }
  }
  // Format 18: Unconditional Branch
  // 1110 0xxx xxxx xxxx
  else if ((instruction & 0xF800) == 0xE000) {
    int32_t offset = (instruction & 0x7FF);
    if (offset & 0x400)
      offset |= 0xFFFFF800; // Sign extend
    offset <<= 1;
    // Branch base is PC+4 in Thumb, i.e. registers[15]+2.
    uint32_t target = registers[15] + 2 + offset;
    LogBranch(registers[15] - 2, target);
    registers[15] = target;
    FlushPipeline(); // Thumb unconditional branch invalidates prefetch
  }
  // Format 19: Long Branch with Link
  // 1111 xxxx xxxx xxxx
  else if ((instruction & 0xF000) == 0xF000) {
    bool H = (instruction >> 11) & 1;
    int32_t offset = instruction & 0x7FF;

    if (!H) { // First instruction (High)
      offset = (offset << 12);
      if (offset & 0x400000)
        offset |= 0xFF800000; // Sign extend
      // LR gets (PC+4+offset). With our PC model, that's registers[15] + 2 +
      // offset.
      registers[14] = registers[15] + 2 + offset;
    } else { // Second instruction (Low)
      uint32_t nextPC =
          registers[15] - 2; // Instruction address + 2 (already incremented)
      uint32_t target = registers[14] + (offset << 1);

      LogBranch(nextPC, target);

      registers[14] = (nextPC + 2) |
                      1; // LR = Return Address + 1 (Thumb) -> Next Instruction
      registers[15] = target;
      FlushPipeline(); // Thumb BL second half invalidates prefetch
    }
  } else {
    // Unknown Thumb instruction
  }
}

void ARM7TDMI::SetZN(uint32_t result) {
  // Thumb helpers still call SetZN; delegate to shared helper for clarity
  UpdateNZFlags(cpsr, result);
}

bool ARM7TDMI::CheckCondition(uint32_t cond) {
  // Use the centralized helper function from ARM7TDMIHelpers
  return ConditionSatisfied(cond, cpsr);
}

void ARM7TDMI::ExecuteSWP(uint32_t instruction) {
  const bool B = (instruction >> 22) & 1;
  const uint32_t rn = ExtractRegisterField(instruction, 16);
  const uint32_t rd = ExtractRegisterField(instruction, 12);
  const uint32_t rm = ExtractRegisterField(instruction, 0);
  const uint32_t addr = registers[rn];

  if (B) {
    uint8_t temp = memory.Read8(addr);
    memory.Write8(addr, registers[rm] & 0xFF);
    registers[rd] = temp;
  } else {
    // Word swap: reads are rotated if misaligned (like LDR)
    uint32_t temp = memory.Read32(addr & ~3u);
    int rot = (addr & 3) * 8;
    if (rot)
      temp = (temp >> rot) | (temp << (32 - rot));
    memory.Write32(addr & ~3u, registers[rm]);
    registers[rd] = temp;
  }
}

void ARM7TDMI::ExecuteMultiply(uint32_t instruction) {
  // ARM Multiply: Cond[31:28] | 000000[27:22] | A[21] | S[20] | Rd[19:16] |
  // Rn[15:12] | Rs[11:8] | 1001[7:4] | Rm[3:0]
  bool A = (instruction >> 21) & 1; // Accumulate bit
  bool S = (instruction >> 20) & 1; // Set Flags bit
  uint32_t rd = ExtractRegisterField(instruction, 16);
  uint32_t rn = ExtractRegisterField(instruction, 12);
  uint32_t rs = ExtractRegisterField(instruction, 8);
  uint32_t rm = ExtractRegisterField(instruction, 0);

  uint32_t op1 = registers[rm];
  uint32_t op2 = registers[rs];
  uint32_t result = op1 * op2;

  if (A) {
    result += registers[rn];
  }

  registers[rd] = result;

  if (S) {
    UpdateNZFlags(cpsr, result);
  }
}

void ARM7TDMI::ExecuteMultiplyLong(uint32_t instruction) {
  // ARM Multiply Long: Cond[31:28] | 00001[27:23] | U[22] | A[21] | S[20] |
  // RdHi[19:16] | RdLo[15:12] | Rs[11:8] | 1001[7:4] | Rm[3:0]
  bool U =
      (instruction >> 22) & 1; // Unsigned/Signed bit (1=Signed, 0=Unsigned)
  bool A = (instruction >> 21) & 1; // Accumulate bit
  bool S = (instruction >> 20) & 1; // Set Flags bit
  uint32_t rdHi = ExtractRegisterField(instruction, 16);
  uint32_t rdLo = ExtractRegisterField(instruction, 12);
  uint32_t rs = ExtractRegisterField(instruction, 8);
  uint32_t rm = ExtractRegisterField(instruction, 0);

  uint64_t op1, op2, result;

  if (U) { // Signed multiply
    op1 = (int64_t)(int32_t)registers[rm];
    op2 = (int64_t)(int32_t)registers[rs];
    result = op1 * op2;
  } else { // Unsigned multiply
    op1 = (uint64_t)registers[rm];
    op2 = (uint64_t)registers[rs];
    result = op1 * op2;
  }

  if (A) {
    uint64_t acc = ((uint64_t)registers[rdHi] << 32) | registers[rdLo];
    result += acc;
  }

  registers[rdLo] = (uint32_t)(result & 0xFFFFFFFF);
  registers[rdHi] = (uint32_t)(result >> 32);

  if (S) {
    // N bit set to bit 63 of result
    SetCPSRFlag(cpsr, CPSR::FLAG_N, (result >> 63) & 1);
    // Z bit set if 64-bit result is zero
    SetCPSRFlag(cpsr, CPSR::FLAG_Z, result == 0);
    // V and C are undefined (V unaffected, C has no meaning)
  }
}

void ARM7TDMI::ExecuteMRS(uint32_t instruction) {
  // ARM MRS: Cond[31:28] | 00010[27:23] | R[22] | 001111[21:16] | Rd[15:12] |
  // 00000000[11:0]
  bool R = (instruction >> 22) & 1; // R bit (0=CPSR, 1=SPSR)
  uint32_t rd = ExtractRegisterField(instruction, 12);

  if (R) { // SPSR
    registers[rd] = spsr;
  } else { // CPSR
    registers[rd] = cpsr;
  }
}

void ARM7TDMI::ExecuteMSR(uint32_t instruction) {
  bool I = (instruction >> 25) & 1;
  bool R = (instruction >> 22) & 1;
  uint32_t mask = (instruction >> 16) & 0xF;
  uint32_t operand = 0;

  if (I) {
    uint32_t rotate = (instruction >> 8) & 0xF;
    uint32_t imm = instruction & 0xFF;
    uint32_t shift = rotate * 2;
    if (shift == 0) {
      operand = imm;
    } else {
      operand = (imm >> shift) | (imm << (32 - shift));
    }
  } else {
    uint32_t rm = instruction & 0xF;
    operand = registers[rm];
  }

  uint32_t currentPSR = R ? spsr : cpsr;
  uint32_t newPSR = currentPSR;

  // NOTE: Real ARM7TDMI enforces privilege rules for MSR (User mode can only
  // update flags). However, many GBA games/libraries assume they can modify
  // CPSR from User mode. We don't enforce the privilege check for compatibility
  // with DirectBoot and games that rely on permissive behavior.

  if (mask & 1)
    newPSR = (newPSR & 0xFFFFFF00) | (operand & 0x000000FF); // Control
  if (mask & 2)
    newPSR = (newPSR & 0xFFFF00FF) | (operand & 0x0000FF00); // Extension
  if (mask & 4)
    newPSR = (newPSR & 0xFF00FFFF) | (operand & 0x00FF0000); // Status
  if (mask & 8)
    newPSR = (newPSR & 0x00FFFFFF) | (operand & 0xFF000000); // Flags

  if (R) {
    // MSR SPSR_*: writes the current mode's SPSR.
    spsr = newPSR;
  } else {
    // MSR CPSR_*: may change mode and/or Thumb state.
    const uint32_t oldMode = cpsr & 0x1F;
    const uint32_t newMode = newPSR & 0x1F;

    // IMPORTANT: SwitchMode() relies on the *current* CPSR mode to choose
    // which bank to save. Switch banks before overwriting CPSR.
    if (oldMode != newMode) {
      SwitchMode(newMode);
    }

    cpsr = newPSR;

    // Keep the execution-state flag consistent with CPSR.T.
    thumbMode = IsThumbMode(cpsr);
  }
}

} // namespace AIO::Emulator::GBA
