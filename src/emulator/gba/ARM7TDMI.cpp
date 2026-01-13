#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <emulator/common/Logger.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <iomanip>
#include <iostream>
#include <sstream>

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

inline bool EnvTruthy(const char *v) {
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}
} // namespace

static void LogBranch([[maybe_unused]] uint32_t from,
                      [[maybe_unused]] uint32_t to) {
  branchLog.push_back({from, to});
  if (branchLog.size() > 50)
    branchLog.pop_front();

  // SMA2 investigation: detect when the game branches to the save/validator
  // object entry. Enable with: AIO_TRACE_SMA2_SAVEOBJ_CALL=1
  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEOBJ_CALL")) {
    const uint32_t masked = to & ~1u;
    if (masked >= 0x08177900u && masked <= 0x08177A50u) {
      uint32_t instr = 0;
      if (g_memoryForLog) {
        instr = g_thumbModeForLog
                    ? g_memoryForLog->ReadInstruction16(from & ~1u)
                    : g_memoryForLog->ReadInstruction32(from & ~3u);
      }
      Logger::Instance().LogFmt(LogLevel::Info, "SMA2",
                                "SAVEOBJ BRANCH from=0x%08x to=0x%08x "
                                "masked=0x%08x instr=0x%08x fromThumb=%d",
                                (unsigned)from, (unsigned)to, (unsigned)masked,
                                (unsigned)instr, g_thumbModeForLog ? 1 : 0);
    }
  }

  // Treat jumps into the middle of the instruction encoding space as invalid.
  // If `to` looks like an ARM opcode rather than an address, something wrote PC
  // with a fetched instruction.
  if ((to & 0xFF000000) == 0xE3000000) {
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "PC CORRUPTION (looks like opcode): 0x%08x -> 0x%08x", from, to);
  }

  // Log branches beyond actual ROM size (4MB = 0x08000000 + 0x400000 =
  // 0x08400000)
  if (to >= 0x08400000 && to < 0x0E000000) {
    uint32_t instr = 0;
    if (g_memoryForLog) {
      instr = g_thumbModeForLog ? g_memoryForLog->ReadInstruction16(from & ~1)
                                : g_memoryForLog->ReadInstruction32(from & ~3);
    }
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "BRANCH BEYOND ROM: from=0x%08x to=0x%08x instr=0x%08x thumbMode=%d",
        from, to, instr, g_thumbModeForLog);
  }

  // Log branches to data regions (lookup table areas)
  if ((to >= 0x08125C00 && to < 0x08127000)) {
    uint32_t instr = 0;
    if (g_memoryForLog) {
      instr = g_thumbModeForLog ? g_memoryForLog->ReadInstruction16(from & ~1)
                                : g_memoryForLog->ReadInstruction32(from & ~3);
    }
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "BRANCH TO DATA SECTION: from=0x%08x to=0x%08x "
                              "instr=0x%08x thumbMode=%d",
                              from, to, instr, g_thumbModeForLog);
  }

  bool valid = (to >= 0x08000000 && to < 0x0E000000) ||
               (to >= 0x03000000 && to < 0x04000000) ||
               (to >= 0x02000000 && to < 0x03000000) || (to < 0x4000) ||
               ((to & 0xFFFFFF00) == 0xFFFFFF00);
  if (!valid) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "BAD BRANCH TO INVALID MEMORY: 0x%08x -> 0x%08x",
                              from, to);
  }
}

static void TracePCWrite(const char *source, uint32_t pcFrom, uint32_t pcTo,
                         uint32_t instruction, uint32_t extra0 = 0,
                         uint32_t extra1 = 0) {
  // Keep this extremely focused; only print when the destination looks wrong.
  const bool looksLikeOpcode = ((pcTo & 0xFF000000) == 0xE3000000) ||
                               ((pcTo & 0xFF000000) == 0xE2000000);
  const bool looksInvalidAddr =
      !((pcTo < 0x00004000) || (pcTo >= 0x02000000 && pcTo < 0x02040000) ||
        (pcTo >= 0x03000000 && pcTo < 0x03008000) ||
        (pcTo >= 0x06000000 && pcTo < 0x06018000) ||
        (pcTo >= 0x08000000 && pcTo < 0x10000000));
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

struct R11WriteEvent {
  uint32_t instrAddr;
  bool thumb;
  uint32_t instruction;
  uint32_t oldVal;
  uint32_t newVal;
};

static std::deque<R11WriteEvent> g_r11WriteHistory;
static bool g_reportedFirstR11Zero = false;
static uint32_t g_lastR11Observed = 0;
static bool g_reportedFirstR11NonZero = false;

static void RecordR11Write(uint32_t instrAddr, bool thumb, uint32_t instruction,
                           uint32_t oldVal, uint32_t newVal) {
  if (oldVal == newVal)
    return;

  g_r11WriteHistory.push_back({instrAddr, thumb, instruction, oldVal, newVal});
  if (g_r11WriteHistory.size() > 32)
    g_r11WriteHistory.pop_front();

  if (!g_reportedFirstR11Zero && oldVal != 0 && newVal == 0) {
    g_reportedFirstR11Zero = true;
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "R11 FIRST->0 at fromPC=0x%08x mode=%c "
                              "instr=0x%08x old=0x%08x new=0x%08x",
                              instrAddr, thumb ? 'T' : 'A', instruction, oldVal,
                              newVal);

    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "R11 HISTORY (most recent last):");
    for (const auto &e : g_r11WriteHistory) {
      Logger::Instance().LogFmt(
          LogLevel::Error, "CPU",
          "  R11H fromPC=0x%08x mode=%c instr=0x%08x old=0x%08x new=0x%08x",
          e.instrAddr, e.thumb ? 'T' : 'A', e.instruction, e.oldVal, e.newVal);
    }
  }
}

static void TraceWatchWrite32(uint32_t instrAddr, bool thumb,
                              uint32_t instruction, uint32_t addr,
                              uint32_t value, uint32_t sp) {
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

ARM7TDMI::ARM7TDMI(GBAMemory &mem) : memory(mem) { Reset(); }

ARM7TDMI::~ARM7TDMI() = default;

void ARM7TDMI::Reset() {
  std::memset(registers, 0, sizeof(registers));
  cpsr = CPUMode::SYSTEM; // System Mode
  spsr = 0;
  thumbMode = false;
  halted = false;

  // Initialize Banked Registers
  r13_svc = 0x03007FE0;
  r14_svc = 0;
  spsr_svc = 0;
  r13_irq = 0x03007FA0;
  r14_irq = 0;
  spsr_irq = 0;
  r13_usr = 0x03007F00;
  r14_usr = 0;

  // Note: LR in System/User mode is game-defined; do not force it.

  // DirectBoot mode: Skip BIOS and jump straight to ROM entry (0x08000000)
  // Hardware state is initialized by GBAMemory::Reset() to match what BIOS
  // would set up
  registers[Register::PC] = 0x08000000;
  registers[Register::SP] = r13_usr; // Initialize SP (User/System)

  // Reset R11 tracking state for debugging.
  g_lastR11Observed = registers[11];
  g_reportedFirstR11Zero = false;
  g_reportedFirstR11NonZero = false;
  g_r11WriteHistory.clear();
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
      // Debug: Log IRQ mode entry
      static int irq_entry_count = 0;
      if (irq_entry_count++ < 10) {
        std::cerr << "[MODE SWITCH] Entering IRQ mode, SP=0x" << std::hex
                  << registers[Register::SP] << " PC=0x" << registers[15]
                  << std::dec << std::endl;
      }
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

  // Debug logging (throttled)
  // static int debugCount = 0;
  // if (debugCount++ == 0 || debugCount % 60000 == 0) {
  //     std::cout << "[CheckIRQ] IME=" << ime << " IE=0x" << std::hex << ie
  //               << " IF=0x" << if_reg << " CPSR.I=" << (IRQDisabled(cpsr) ? 1
  //               : 0)
  //               << " PC=0x" << registers[15] << std::dec << std::endl;
  // }

  // Wake from HALT/STOP/IntrWait if any enabled interrupt is pending.
  // Do NOT auto-resume debugger breakpoints/stepback halts.
  if (halted && sleepHalt && (ie & if_reg)) {
    halted = false;
    sleepHalt = false;
  }

  if (!(ime & 1))
    return;
  if (IRQDisabled(cpsr))
    return; // IRQ disabled in CPSR

  if (ie & if_reg) {
    // Calculate triggered interrupts NOW before PPU/Timers run
    // On real GBA, BIOS reads IE & IF atomically at IRQ entry
    uint16_t triggered = ie & if_reg;

    const bool traceIrqEntry = EnvFlagCached("AIO_TRACE_IRQ_ENTRY");
    const uint32_t spBeforeIrq = registers[Register::SP];
    const uint32_t pcBeforeIrq = registers[Register::PC];
    const uint32_t cpsrBeforeIrq = cpsr;

    // Update BIOS_IF immediately (before PPU can change IF)
    // GBATEK: BIOS_IF is a 16-bit value at 0x03007FF8 (do NOT do 32-bit
    // accesses here).
    uint16_t biosIF = memory.Read16(0x03007FF8);
    TraceWatchWrite32(currentInstrAddr, currentInstrThumb,
                      currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                      0x03007FF8, (uint16_t)(biosIF | triggered),
                      registers[13]);
    memory.Write16(0x03007FF8, (uint16_t)(biosIF | triggered));

    // std::cout << "[IRQ ENTRY] Triggered=0x" << std::hex << triggered << "
    // BIOS_IF updated=0x" << (biosIF | triggered) << std::dec << std::endl;

    // Also write to a scratch location the BIOS can read
    // We'll use 0x03007FF4 as a temp storage for triggered bits
    memory.Write16(0x03007FF4, triggered);

    // Capture current execution state before switching modes.
    // Prefer CPSR.T but tolerate occasional thumbMode/CPSR divergence during
    // debugging.
    const bool cpsrThumb = IsThumbMode(cpsr);
    const bool wasThumb = thumbMode || cpsrThumb;

    // Save CPSR to SPSR_irq (restore target state on SUBS/MOVS PC return).
    uint32_t oldCpsr = cpsr;
    SetCPSRFlag(oldCpsr, CPSR::FLAG_T, wasThumb);

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

    // Switch to IRQ Mode
    SwitchMode(CPUMode::IRQ);
    spsr = oldCpsr;
    spsr_irq = oldCpsr; // Also save to banked SPSR for System Mode return

    if (traceIrqEntry) {
      const uint32_t handlerPtr = memory.Read32(0x03007FFCu);
      Logger::Instance().LogFmt(
          LogLevel::Error, "CPU",
          "IRQ ENTRY: trig=0x%04x PC=0x%08x SP(before)=0x%08x "
          "CPSR(before)=0x%08x -> mode=0x%02x SP(after)=0x%08x "
          "CPSR(mid)=0x%08x SPSR(saved)=0x%08x handler[0x03007FFC]=0x%08x",
          (unsigned)triggered, pcBeforeIrq, spBeforeIrq, cpsrBeforeIrq,
          (unsigned)GetCPUMode(cpsr), registers[Register::SP], cpsr, oldCpsr,
          handlerPtr);
    }

    // Another test comment

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

    // std::cout << "[IRQ EXIT] LR=0x" << std::hex << registers[Register::LR]
    //           << " Jumping to BIOS IRQ=0x" << ExceptionVector::IRQ <<
    //           std::dec << std::endl;

    // Jump to BIOS IRQ Trampoline at ExceptionVector::IRQ
    registers[Register::PC] = ExceptionVector::IRQ;
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
  const uint32_t lrBefore = registers[14];
  const bool thumbBefore = thumbMode;
  const uint32_t r0BeforeStep = registers[0];
  const uint32_t r2BeforeStep = registers[2];
  const uint32_t spBeforeStep = registers[13];

  // Capture instruction context once per Step() so traces can't accidentally
  // attribute a register write to the wrong opcode.
  currentInstrAddr = registers[15];
  currentInstrThumb = thumbMode;
  currentOp16 = 0;
  currentOp32 = 0;

  // If R11 changes (especially to zero) without going through our explicit
  // decode/execute trace call sites, catch it here.
  const uint32_t r11Now = registers[11];
  if (!g_reportedFirstR11NonZero && r11Now != 0) {
    g_reportedFirstR11NonZero = true;
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "R11 FIRST!=0 observed at PC=0x%08x mode=%c R11=0x%08x",
        currentInstrAddr, currentInstrThumb ? 'T' : 'A', r11Now);
  }

  if (kEnableHeavyCpuTraces && currentInstrAddr >= 0x08007310 &&
      currentInstrAddr <= 0x08007340) {
    Logger::Instance().LogFmt(
        LogLevel::Error, "CPU",
        "SMA2 EARLY PC=0x%08x mode=%c R8=0x%08x R11=0x%08x SP=0x%08x LR=0x%08x",
        currentInstrAddr, currentInstrThumb ? 'T' : 'A', registers[8],
        registers[11], registers[13], registers[14]);
  }

  // SMA2 save-validation root-cause dump.
  // The game takes the "Your saved data is corrupt" path when a validator
  // returns 0, then rewrites the EEPROM header. This one-shot dump captures the
  // validator return and the in-RAM buffer it validated. Enable with:
  // AIO_TRACE_SMA2_SAVEVALID=1
  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEVALID")) {
    static bool dumped = false;
    if (!dumped && currentInstrThumb && currentInstrAddr == 0x080073D0u) {
      dumped = true;

      const uint32_t r0 = registers[0];
      const uint32_t r1 = registers[1];
      const uint32_t r2 = registers[2];
      const uint32_t r3 = registers[3];
      const uint32_t sp = registers[13];
      const uint32_t lr = registers[14];

      std::cerr << "[SMA2 SAVEVALID] PC=0x" << std::hex << currentInstrAddr
                << " R0(ret)=0x" << r0 << " R1(buf)=0x" << r1 << " R2=0x" << r2
                << " R3=0x" << r3 << " SP=0x" << sp << " LR=0x" << lr
                << " CPSR=0x" << cpsr << std::dec << "\n";

      auto dumpBytes = [&](uint32_t addr, size_t len) {
        std::cerr << "[SMA2 SAVEVALID] mem[0x" << std::hex << addr << "] ("
                  << std::dec << len << " bytes):\n";
        for (size_t i = 0; i < len; ++i) {
          if ((i % 16) == 0) {
            std::cerr << "  0x" << std::hex << (addr + (uint32_t)i) << ": ";
          }
          const uint8_t b = memory.Read8(addr + (uint32_t)i);
          std::cerr << std::hex << std::setw(2) << std::setfill('0')
                    << (unsigned)b;
          if ((i % 16) != 15)
            std::cerr << " ";
          if ((i % 16) == 15)
            std::cerr << std::dec << "\n";
        }
        if ((len % 16) != 0)
          std::cerr << std::dec << "\n";
      };

      // Dump the buffer the validator was pointed at (common observed:
      // 0x03007BC8).
      if (r1 >= 0x02000000 && r1 < 0x04000000) {
        dumpBytes(r1, 0x80);
      }

      // Dump the EWRAM staging region commonly used by the SMA2 EEPROM
      // routines. This helps distinguish "validator object is zero" from
      // "decoded header bytes are wrong".
      dumpBytes(0x020003C0, 0x80);

      // Dump the IWRAM scratch buffers where the EEPROM DMA bitstream is
      // placed.
      dumpBytes(0x03007CC0, 0x80);
      dumpBytes(0x03007CE4, 0x80);

      // Also dump the tail of IWRAM where BIOS variables live, in case the
      // validator relies on them.
      dumpBytes(0x03007B80, 0x100);
    }
  }
  if (r11Now != g_lastR11Observed) {
    const uint32_t instr =
        currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
    RecordR11Write(currentInstrAddr, currentInstrThumb, instr,
                   g_lastR11Observed, r11Now);

    if (!g_reportedFirstR11Zero && g_lastR11Observed != 0 && r11Now == 0) {
      g_reportedFirstR11Zero = true;
      Logger::Instance().LogFmt(
          LogLevel::Error, "CPU",
          "R11 FIRST->0 (detected in Step prologue) at fromPC=0x%08x mode=%c "
          "instr=0x%08x old=0x%08x new=0x%08x",
          currentInstrAddr, currentInstrThumb ? 'T' : 'A', instr,
          g_lastR11Observed, r11Now);

      Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                "R11 HISTORY (most recent last):");
      for (const auto &e : g_r11WriteHistory) {
        Logger::Instance().LogFmt(
            LogLevel::Error, "CPU",
            "  R11H fromPC=0x%08x mode=%c instr=0x%08x old=0x%08x new=0x%08x",
            e.instrAddr, e.thumb ? 'T' : 'A', e.instruction, e.oldVal,
            e.newVal);
      }
    }

    g_lastR11Observed = r11Now;
  }

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

  // Disable verbose step logging - too much output
  // std::cerr << "[Step PC=0x" << std::hex << registers[15] << " SP=0x" <<
  // registers[13] << std::dec << "]" << std::endl;

  CheckInterrupts();

  if (halted) {
    return;
  }

  // Universal PC region validation and mirroring
  // Canonicalize IWRAM/EWRAM/VRAM/ROM
  uint32_t pc = registers[15];
  if (pc >= 0x03008000 && pc < 0x04000000)
    pc = 0x03000000 | (pc & 0x7FFF);
  if (pc >= 0x02040000 && pc < 0x03000000)
    pc = 0x02000000 | (pc & 0x3FFFF);
  if (pc >= 0x06018000 && pc < 0x07000000)
    pc = 0x06000000 | (pc & 0x17FFF);
  if (pc >= 0x08000000 && pc < 0x10000000)
    pc = 0x08000000 | (pc & 0x1FFFFFF);
  registers[15] = pc;

  // Validate PC region
  bool valid = false;
  if (pc < 0x00004000)
    valid = true; // BIOS
  if (pc >= 0x02000000 && pc < 0x02040000)
    valid = true; // EWRAM
  if (pc >= 0x03000000 && pc < 0x03008000)
    valid = true; // IWRAM
  if (pc >= 0x06000000 && pc < 0x06018000)
    valid = true; // VRAM
  if (pc >= 0x08000000 && pc < 0x10000000)
    valid = true; // ROM
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

  pc = registers[15];

  // ...existing code...

  // Trace Interrupt Handler
  /*
  if ((pc >= 0x180 && pc <= 0x220) || (pc >= 0x03002364 && pc <= 0x03002600) ||
  (pc >= 0x08000578 && pc <= 0x08000600) || (pc >= 0x08000400 && pc <=
  0x08000550)) { uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) :
  memory.ReadInstruction32(pc); std::cout << "[Trace] PC=0x" << std::hex << pc
  << " Instr=0x" << instr << " Mode=" << (thumbMode?"Thumb":"ARM")
                 << " R0=" << registers[0] << " R1=" << registers[1] << " R2="
  << registers[2] << " R3=" << registers[3] << " R12=" << registers[12] << "
  LR=" << registers[14] << std::dec << std::endl;
  }
  */

  // DEBUG: Trace near crash point 0x300330c
  // Enable with: AIO_TRACE_CPU_CRASH=1
  static const bool crashTraceRequested =
      (std::getenv("AIO_TRACE_CPU_CRASH") != nullptr);
  static bool crashTraceEnabled = crashTraceRequested;
  static int crashTraceCount = 0;

  // Trace caller at ROM before BX R0
  if (crashTraceEnabled && pc >= 0x8032400 && pc <= 0x8032420) {
    uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc)
                                 : memory.ReadInstruction32(pc);
    std::cout << "[CallerTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr
              << " R0=0x" << registers[0] << " R1=0x" << registers[1]
              << " R11=0x" << registers[11] << " LR=0x" << registers[14]
              << " Mode=" << (thumbMode ? "T" : "A") << std::dec << std::endl;
  }

  // Trace early ROM startup (first 100 instructions from 0x08000000)
  static int earlyTraceCount = 0;
  // DISABLED: Too verbose during boot
  // if (pc >= 0x08000000 && pc <= 0x08000200 && earlyTraceCount < 100) {
  //     earlyTraceCount++;
  //     uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) :
  //     memory.ReadInstruction32(pc); std::cout << "[EarlyTrace] PC=0x" <<
  //     std::hex << pc << " Instr=0x" << instr
  //               << " R0=0x" << registers[0] << " R1=0x" << registers[1]
  //               << " R13=0x" << registers[13] << " LR=0x" << registers[14]
  //               << std::dec << std::endl;
  // }

  // Trace 0x800023d to 0x8000996 (before DMA#1)
  static int preDmaTraceCount = 0;
  // DISABLED: Too verbose during boot
  // if (pc >= 0x0800023c && pc <= 0x080009a0 && preDmaTraceCount < 200) {
  //     preDmaTraceCount++;
  //     uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) :
  //     memory.ReadInstruction32(pc); std::cout << "[PreDMA] PC=0x" << std::hex
  //     << pc << " Instr=0x" << instr
  //               << " R0=0x" << registers[0] << " R1=0x" << registers[1]
  //               << " R2=0x" << registers[2] << " R3=0x" << registers[3]
  //               << " SP=0x" << registers[13]
  //               << (thumbMode?" T":" A") << std::dec << std::endl;
  // }

  // Trace the entry point 0x30032b0 to 0x3003400 (expanded to cover branch
  // targets)
  if (crashTraceRequested && crashTraceEnabled && pc >= 0x30032b0 &&
      pc <= 0x3003400) {
    uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc)
                                 : memory.ReadInstruction32(pc);
    std::cout << "[EntryTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr
              << " R0=0x" << registers[0] << " R1=0x" << registers[1]
              << " R11=0x" << registers[11] << " LR=0x" << registers[14]
              << " Mode=" << (thumbMode ? "T" : "A") << std::dec << std::endl;
  }
  if (pc == 0x3003308) {
    // This is the LDR R0, [R12, R0, LSL #4] instruction
    // R0 should be a small index (0-15 or so)
    if (registers[0] > 20) {
      std::cout << "[CRASH TRACE] LDR at 0x3003308 with BAD R0=0x" << std::hex
                << registers[0] << " R11=0x" << registers[11] << " R12=0x"
                << registers[12] << std::dec << std::endl;
    }
  }
  if (crashTraceRequested && pc >= 0x3003300 && pc <= 0x3003320) {
    crashTraceEnabled = true;
  }
  if (crashTraceRequested && crashTraceEnabled && crashTraceCount < 200) {
    crashTraceCount++;
    uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc)
                                 : memory.ReadInstruction32(pc);
    std::cout << "[CrashTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr
              << " R0=0x" << registers[0] << " R11=0x" << registers[11]
              << " R12=0x" << registers[12] << " LR=0x" << registers[14]
              << " R1=0x" << registers[1] << " R2=0x" << registers[2]
              << " SP=0x" << registers[13] << std::dec << std::endl;
  }

  // ...existing code...

  lastPC = pc;

  // BIOS handling:
  // We do not ship a full BIOS ROM, but we *do* install a real
  // instruction-level IRQ vector + trampoline in the BIOS region (see
  // GBAMemory::InitializeHLEBIOS/Reset). For correctness, let that trampoline
  // execute as normal instructions. For other BIOS entry points that games may
  // call directly, we still provide HLE.
  if (pc < 0x4000) {
    const uint32_t pcAligned = thumbMode ? (pc & ~1u) : (pc & ~3u);

    const bool inIrqVector = (pcAligned == 0x00000018u);
    // The IRQ trampoline is installed in BIOS space; execute it as
    // normal ARM instructions rather than HLE. Keep the address range
    // in sync with the layout used in GBAMemory::InitializeHLEBIOS.
    const bool inIrqTrampoline =
        (pcAligned >= 0x00003F00u && pcAligned < 0x00003F50u);

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
    const uint16_t instruction = memory.ReadInstruction16(instrAddr);

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

    // SMA2 investigation: trace the save/repair decision window.
    // Enable with: AIO_TRACE_SMA2_SAVE_WINDOW=1
    // This range includes the validation/branching that decides whether to
    // preserve the existing header or to zero/init fields (leading to FEBC
    // checksum).
    const bool traceSaveWin = EnvFlagCached("AIO_TRACE_SMA2_SAVE_WINDOW");
    // SMA2 investigation: dump the exact EWRAM bytes the checksum routine
    // validates. Enable with: AIO_TRACE_SMA2_SAVE_DUMP=1
    const bool traceSaveDump = EnvFlagCached("AIO_TRACE_SMA2_SAVE_DUMP");
    const bool inSaveWinRange =
        (instrAddr >= 0x08007300 && instrAddr <= 0x08007520);

    // SMA2 investigation: trace status propagation from the EEPROM
    // read/validate routine. Enable with: AIO_TRACE_SMA2_EEPCALL=1
    const bool traceEepCall = EnvFlagCached("AIO_TRACE_SMA2_EEPCALL");
    const bool inEepFuncRange =
        (instrAddr >= 0x0809E000 && instrAddr <= 0x0809E360);
    const bool inEepMiniRange =
        (instrAddr >= 0x0809E020 && instrAddr <= 0x0809E090);

    // SMA2 investigation: trace the EEPROM init/dispatch prologue just before
    // the DMA-driven EEPROM routines (where the validator object at 0x03007BC8
    // is set up). Enable with: AIO_TRACE_SMA2_EEP_INIT=1
    const bool traceEepInit = EnvFlagCached("AIO_TRACE_SMA2_EEP_INIT");
    const bool inEepInitRange =
        (instrAddr >= 0x0809DF80 && instrAddr <= 0x0809E120);

    if (traceEepInit && inEepInitRange) {
      static int eepInitLogs = 0;
      if (eepInitLogs < 900) {
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 EEPINIT pre PC=0x%08x op=0x%04x R0=0x%08x R1=0x%08x "
            "R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x "
            "SP=0x%08x LR=0x%08x CPSR=0x%08x",
            instrAddr, instruction, registers[0], registers[1], registers[2],
            registers[3], registers[4], registers[5], registers[6],
            registers[7], registers[13], registers[14], cpsr);
        eepInitLogs++;
      }
    }

    // This routine is large; keep logging extremely targeted to avoid drowning
    // the run.
    const bool isEepCallSite =
        (instrAddr == 0x0809E1B4u) || (instrAddr == 0x0809E226u) ||
        (instrAddr == 0x0809E23Eu) || (instrAddr == 0x0809E25Cu) ||
        (instrAddr == 0x0809E272u) || (instrAddr == 0x0809E2D6u) ||
        (instrAddr == 0x0809E2ECu) || (instrAddr == 0x0809E326u) ||
        (instrAddr == 0x0809E33Au) || (instrAddr == 0x0809E352u) ||
        (instrAddr == 0x0809E3B4u);

    if (traceEepCall && (inEepMiniRange || isEepCallSite)) {
      static int eepMiniLogs = 0;
      if (eepMiniLogs < 120) {
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 EEPCALL pre PC=0x%08x op=0x%04x R0=0x%08x R1=0x%08x "
            "R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x "
            "SP=0x%08x LR=0x%08x CPSR=0x%08x",
            instrAddr, instruction, registers[0], registers[1], registers[2],
            registers[3], registers[4], registers[5], registers[6],
            registers[7], registers[13], registers[14], cpsr);
        eepMiniLogs++;
      }
    }
    if (traceSaveWin && inSaveWinRange) {
      static int saveWinLogs = 0;
      if (saveWinLogs < 1400) {
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 SAVEWIN pre PC=0x%08x op=0x%04x R0=0x%08x R1=0x%08x "
            "R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x SP=0x%08x LR=0x%08x "
            "CPSR=0x%08x",
            instrAddr, instruction, registers[0], registers[1], registers[2],
            registers[3], registers[4], registers[5], registers[13],
            registers[14], cpsr);
        saveWinLogs++;
      }
    }

    // SMA2 investigation: the checksum/validate branch that selects the repair
    // path. We dump the exact bytes at the pointers the routine uses (R5 base
    // and R2 cursor).
    if (traceSaveDump && (instrAddr == 0x0800741C || instrAddr == 0x08007438 ||
                          instrAddr == 0x0800743A)) {
      static int dumpLogs = 0;
      if (dumpLogs < 60) {
        auto DumpBytes = [&](uint32_t addr, int count) {
          std::ostringstream oss;
          oss << std::hex << std::setfill('0');
          for (int i = 0; i < count; i++) {
            const uint8_t b = memory.Read8(addr + static_cast<uint32_t>(i));
            if (i)
              oss << ' ';
            oss << std::setw(2) << static_cast<int>(b);
          }
          return oss.str();
        };

        const uint32_t base = registers[5];
        const uint32_t cursor = registers[2];

        // Use small, readable windows: base[0..15] and cursor[-4..+11].
        const uint32_t cursorStart = (cursor >= 4) ? (cursor - 4) : 0;
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 SAVEDUMP PC=0x%08x base(R5)=0x%08x cursor(R2)=0x%08x "
            "R0=0x%08x R1=0x%08x R4=0x%08x CPSR=0x%08x | base[0..15]=%s | "
            "cur[-4..+11]=%s",
            instrAddr, base, cursor, registers[0], registers[1], registers[4],
            cpsr, DumpBytes(base, 16).c_str(),
            DumpBytes(cursorStart, 16).c_str());
        dumpLogs++;
      }
    }
    if (kEnableHeavyCpuTraces &&
        (pcAligned == 0x08007320 || pcAligned == 0x0800705A ||
         pcAligned == 0x08007BBC)) {
      Logger::Instance().LogFmt(
          LogLevel::Error, "CPU",
          "THUMB EXEC fetchAddr=0x%08x pc=0x%08x op=0x%04x", pcAligned, pc,
          instruction);
    }

    // DEBUG: Trace instruction at PC near 0x80323c6 (disabled)
    // if (pc >= 0x80323c0 && pc <= 0x80323d0) {
    //     std::cout << "[THUMB] PC=0x" << std::hex << pc
    //               << " instr=0x" << instruction << std::dec << std::endl;
    // }

    // Advance to next instruction. Keep the Thumb-visible PC semantics
    // consistent by maintaining R15 as the current instruction address (not
    // PC+4). When an instruction uses PC as an operand, compute it as instrAddr
    // + 4. Compute Thumb-visible PC (used by instructions that read PC). In
    // Thumb state, PC reads as current instruction address + 4.
    const uint32_t pcValue = (instrAddr + 4u);

    // Advance to next instruction.
    registers[15] = instrAddr + 2u;
    DecodeThumb(instruction, pcValue);

    // SMA2 investigation: trace control flow and key PC-relative loads in the
    // EEPROM helper window. Enable with: AIO_TRACE_SMA2_EEPWIN_CPU=1
    if (EnvFlagCached("AIO_TRACE_SMA2_EEPWIN_CPU")) {
      const uint32_t winLo = 0x0809E1B0u;
      const uint32_t winHi = 0x0809E330u;
      if (instrAddr >= winLo && instrAddr <= winHi) {
        static int winLogs = 0;
        if (winLogs < 2000) {
          const uint32_t pcAfter = registers[15];
          const uint32_t seqNext = instrAddr + 2u;
          const uint32_t n = (cpsr >> 31) & 1u;
          const uint32_t z = (cpsr >> 30) & 1u;
          const uint32_t c = (cpsr >> 29) & 1u;
          const uint32_t v = (cpsr >> 28) & 1u;

          auto signExtend = [](uint32_t value, uint32_t bits) -> int32_t {
            // Must perform the right-shift on a signed type to get arithmetic
            // sign extension.
            const uint32_t shift = 32u - bits;
            const int32_t shifted = (int32_t)(value << shift);
            return (shifted >> shift);
          };

          bool logged = false;

          // Thumb conditional branch: 1101 cccc oooooooo (cccc != 1111)
          if ((instruction & 0xF000u) == 0xD000u &&
              ((instruction >> 8) & 0xFu) != 0xFu) {
            const uint32_t cond = (instruction >> 8) & 0xFu;
            const uint32_t imm8 = instruction & 0xFFu;
            const int32_t off = signExtend((imm8 << 1u), 9u);
            const uint32_t target = (uint32_t)((int64_t)pcValue + (int64_t)off);
            const bool taken = (pcAfter == target);
            Logger::Instance().LogFmt(
                LogLevel::Info, "CPU",
                "SMA2 EEPWIN BCOND PC=0x%08x op=0x%04x cond=%u off=%d "
                "tgt=0x%08x taken=%u -> PC=0x%08x NZCV=%u%u%u%u",
                instrAddr, (unsigned)instruction, (unsigned)cond, (int)off,
                (unsigned)target, (unsigned)(taken ? 1 : 0), (unsigned)pcAfter,
                (unsigned)n, (unsigned)z, (unsigned)c, (unsigned)v);
            logged = true;
            winLogs++;
          }

          // Thumb unconditional branch: 11100 ooooooooooo
          if (!logged && (instruction & 0xF800u) == 0xE000u) {
            const uint32_t imm11 = instruction & 0x7FFu;
            const int32_t off = signExtend((imm11 << 1u), 12u);
            const uint32_t target = (uint32_t)((int64_t)pcValue + (int64_t)off);
            Logger::Instance().LogFmt(
                LogLevel::Info, "CPU",
                "SMA2 EEPWIN B PC=0x%08x op=0x%04x off=%d tgt=0x%08x -> "
                "PC=0x%08x NZCV=%u%u%u%u",
                instrAddr, (unsigned)instruction, (int)off, (unsigned)target,
                (unsigned)pcAfter, (unsigned)n, (unsigned)z, (unsigned)c,
                (unsigned)v);
            logged = true;
            winLogs++;
          }

          // Thumb LDR literal: 01001 RRR oooooooo (addr = Align(pcValue,4) +
          // imm8*4)
          if (!logged && (instruction & 0xF800u) == 0x4800u) {
            const uint32_t rd = (instruction >> 8) & 0x7u;
            const uint32_t imm8 = instruction & 0xFFu;
            const uint32_t base = pcValue & ~3u;
            const uint32_t addr = base + (imm8 << 2u);
            const uint32_t loaded = registers[rd];
            const uint32_t expected = memory.Read32(addr);
            Logger::Instance().LogFmt(
                LogLevel::Info, "CPU",
                "SMA2 EEPWIN LDRLIT PC=0x%08x op=0x%04x Rd=%u addr=0x%08x "
                "loaded=0x%08x expected=0x%08x NZCV=%u%u%u%u",
                instrAddr, (unsigned)instruction, (unsigned)rd, (unsigned)addr,
                (unsigned)loaded, (unsigned)expected, (unsigned)n, (unsigned)z,
                (unsigned)c, (unsigned)v);
            logged = true;
            winLogs++;
          }

          // Fallback: any PC change in the window.
          if (!logged && pcAfter != seqNext) {
            Logger::Instance().LogFmt(
                LogLevel::Info, "CPU",
                "SMA2 EEPWIN FLOW PC=0x%08x op=0x%04x -> PC=0x%08x "
                "(seq=0x%08x) LR=0x%08x SP=0x%08x NZCV=%u%u%u%u",
                instrAddr, (unsigned)instruction, (unsigned)pcAfter,
                (unsigned)seqNext, (unsigned)registers[14],
                (unsigned)registers[13], (unsigned)n, (unsigned)z, (unsigned)c,
                (unsigned)v);
            winLogs++;
          }
        }
      }
    }

    // SMA2 investigation: trace any control-flow transition into the
    // save-object routine area. This catches indirect calls that won't show up
    // as a normal branch trace (e.g. POP {pc}, LDR pc, [..]). Enable with:
    // AIO_TRACE_SMA2_SAVEOBJ_TRANS=1
    if (EnvFlagCached("AIO_TRACE_SMA2_SAVEOBJ_TRANS")) {
      const uint32_t pcAfter = registers[15];
      if (pcAfter >= 0x08177900u && pcAfter <= 0x08177A50u) {
        static int transLogsT = 0;
        if (transLogsT < 200) {
          Logger::Instance().LogFmt(
              LogLevel::Info, "CPU",
              "SMA2 SAVEOBJ TRANS (T) fromPC=0x%08x op=0x%04x -> PC=0x%08x "
              "LR=0x%08x SP=0x%08x R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x "
              "CPSR=0x%08x",
              instrAddr, instruction, pcAfter, registers[14], registers[13],
              registers[0], registers[1], registers[2], registers[3], cpsr);
          transLogsT++;
        }
      }
    }

    // SMA2 investigation: trace R0 changes and the callsite return value.
    if (traceEepCall) {
      if (inEepFuncRange && registers[0] != r0BeforeStep) {
        static int eepR0Logs = 0;
        if (eepR0Logs < 600) {
          Logger::Instance().LogFmt(
              LogLevel::Info, "CPU",
              "SMA2 EEPCALL R0WRITE PC=0x%08x op=0x%04x oldR0=0x%08x "
              "newR0=0x%08x SP=0x%08x->0x%08x LR=0x%08x CPSR=0x%08x",
              instrAddr, instruction, r0BeforeStep, registers[0], spBeforeStep,
              registers[13], registers[14], cpsr);
          eepR0Logs++;
        }
      }

      if (inEepFuncRange && registers[2] != r2BeforeStep) {
        static int eepR2Logs = 0;
        if (eepR2Logs < 600) {
          Logger::Instance().LogFmt(
              LogLevel::Info, "CPU",
              "SMA2 EEPCALL R2WRITE PC=0x%08x op=0x%04x oldR2=0x%08x "
              "newR2=0x%08x R0=0x%08x SP=0x%08x->0x%08x LR=0x%08x CPSR=0x%08x",
              instrAddr, instruction, r2BeforeStep, registers[2], registers[0],
              spBeforeStep, registers[13], registers[14], cpsr);
          eepR2Logs++;
        }
      }

      // Log the callsite return value that drives the BEQ at 0x080073D4.
      if (instrAddr == 0x080073D0) {
        static int eepRetLogs = 0;
        if (eepRetLogs < 80) {
          Logger::Instance().LogFmt(LogLevel::Info, "CPU",
                                    "SMA2 EEPCALL RET PC=0x%08x R0=0x%08x "
                                    "SP=0x%08x LR=0x%08x CPSR=0x%08x",
                                    instrAddr, registers[0], registers[13],
                                    registers[14], cpsr);
          eepRetLogs++;
        }
      }
    }

    // SMA2 investigation: log taken branches/jumps within the save window.
    // This helps pinpoint the exact conditional decision that selects the
    // repair path.
    if (traceSaveWin && inSaveWinRange) {
      const uint32_t expectedNext = instrAddr + 2u;
      const uint32_t actualNext = registers[15];
      if (actualNext != expectedNext) {
        static int saveWinBranchLogs = 0;
        if (saveWinBranchLogs < 400) {
          Logger::Instance().LogFmt(
              LogLevel::Info, "CPU",
              "SMA2 SAVEWIN flow PC=0x%08x op=0x%04x next=0x%08x "
              "(expected=0x%08x) T=%d CPSR=0x%08x",
              instrAddr, instruction, actualNext, expectedNext,
              thumbMode ? 1 : 0, cpsr);
          saveWinBranchLogs++;
        }
      }
    }

    // Post-exec: for the CMP that gates the checksum loop, validate NZCV
    // results.
    if (traceSaveWin && instrAddr == 0x080074D0) {
      static int cmpPostLogs = 0;
      if (cmpPostLogs < 80) {
        Logger::Instance().LogFmt(
            LogLevel::Info, "CPU",
            "SMA2 SAVEWIN post CMP PC=0x%08x op=0x%04x CPSR=0x%08x (R3=0x%08x)",
            instrAddr, instruction, cpsr, registers[3]);
        cmpPostLogs++;
      }
    }

    // Post-exec: trace LR changes (especially LR becoming even in Thumb, which
    // breaks interworking returns).
    if (kEnableHeavyCpuTraces && registers[14] != lrBefore) {
      static int lrLogCount = 0;
      if (lrLogCount++ < 200) {
        Logger::Instance().LogFmt(LogLevel::Info, "CPU",
                                  "LR WRITE fromPC=0x%08x mode=%c op=0x%04x "
                                  "oldLR=0x%08x newLR=0x%08x (thumbNow=%d)",
                                  currentInstrAddr, thumbBefore ? 'T' : 'A',
                                  currentOp16, lrBefore, registers[14],
                                  thumbMode ? 1 : 0);
      }
    }
  } else {
    g_memoryForLog = &memory;
    g_thumbModeForLog = false;
    const uint32_t instrAddr = pc & ~3u;
    const uint32_t instruction = memory.ReadInstruction32(instrAddr);

    // Capture authoritative instruction context for tracing.
    currentInstrAddr = instrAddr;
    currentInstrThumb = false;
    currentOp16 = 0;
    currentOp32 = instruction;

    registers[15] = instrAddr + 4u;
    Decode(instruction);

    // SMA2 investigation: trace any control-flow transition into the
    // save-object routine area. Enable with: AIO_TRACE_SMA2_SAVEOBJ_TRANS=1
    if (EnvFlagCached("AIO_TRACE_SMA2_SAVEOBJ_TRANS")) {
      const uint32_t pcAfter = registers[15];
      if (pcAfter >= 0x08177900u && pcAfter <= 0x08177A50u) {
        static int transLogsA = 0;
        if (transLogsA < 200) {
          Logger::Instance().LogFmt(
              LogLevel::Info, "CPU",
              "SMA2 SAVEOBJ TRANS (A) fromPC=0x%08x op=0x%08x -> PC=0x%08x "
              "LR=0x%08x SP=0x%08x R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x "
              "CPSR=0x%08x",
              instrAddr, instruction, pcAfter, registers[14], registers[13],
              registers[0], registers[1], registers[2], registers[3], cpsr);
          transLogsA++;
        }
      }
    }

    // Post-exec: trace LR changes.
    if (kEnableHeavyCpuTraces && registers[14] != lrBefore) {
      static int lrLogCountA = 0;
      if (lrLogCountA++ < 200) {
        Logger::Instance().LogFmt(LogLevel::Info, "CPU",
                                  "LR WRITE fromPC=0x%08x mode=%c op=0x%08x "
                                  "oldLR=0x%08x newLR=0x%08x (thumbNow=%d)",
                                  currentInstrAddr, thumbBefore ? 'T' : 'A',
                                  currentOp32, lrBefore, registers[14],
                                  thumbMode ? 1 : 0);
      }
    }
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
    // ARM SWI has a 24-bit immediate; GBA BIOS uses the low 8 bits as the
    // function number.
    ExecuteSWI(instruction & 0xFF);
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

  // DEBUG: Trace branches in audio code
  if (currentPC >= 0x30032b0 && currentPC <= 0x3003400) {
    std::cout << "[BRANCH DEBUG] PC=0x" << std::hex << (currentPC - 4)
              << " instr=0x" << instruction << " offset=" << offset
              << " target=0x" << target << std::dec << std::endl;
  }

  LogBranch(currentPC - 4, target); // Log from actual instruction address
  registers[Register::PC] = target;
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
    result = rnVal + op2 + carry;
    cOut = (rnVal + op2 < rnVal) ||
           (rnVal + op2 + carry <
            rnVal + op2); // Carry if either addition overflows
    {
      bool sign1 = (rnVal >> 31) & 1;
      bool sign2 = (op2 >> 31) & 1;
      bool signR = (result >> 31) & 1;
      overflow = (sign1 == sign2 && sign1 != signR);
    }
    break;
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
    uint32_t rm = ExtractRegisterField(instruction, 0);
    offset = registers[rm];
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
      registers[rd] = memory.Read8(targetAddr);
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb,
                     currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                     old, registers[rd]);
      }
    } else {
      uint32_t val = memory.Read32(targetAddr);
      if (rd == Register::PC) {
        const uint32_t from = registers[Register::PC] - 4;
        TracePCWrite("LDR", from, val, instruction, targetAddr, rn);
        LogBranch(from, val);
      }
      const uint32_t old = registers[rd];
      registers[rd] = val;
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb,
                     currentInstrThumb ? (uint32_t)currentOp16 : currentOp32,
                     old, registers[rd]);
      }
      if (rd == 11) {
        {
          const uint32_t tracedInstr =
              currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
          TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                        registers[rd]);
          RecordR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                         registers[rd]);
        }
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
      {
        const uint32_t tracedInstr =
            currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
        TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                      registers[rn]);
        RecordR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                       registers[rn]);
      }
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

  // One-time pre-exec snapshot for the known failing instruction.
  if (instrAddr == 0x08001774 && instruction == 0x8818DA2F) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "ABOUT TO EXEC LDMDA R8,...,PC at 0x%08x: "
                              "R8=0x%08x SP=0x%08x CPSR=0x%08x",
                              instrAddr, registers[8], registers[13], cpsr);
  }

  // Debug: Log LDM/STM near crash location
  if (registers[15] >= 0x809e390 && registers[15] <= 0x809e3a0) {
    std::cerr << "[LDM/STM DEBUG] PC=0x" << std::hex << (registers[15] - 8)
              << " Instruction=0x" << instruction << " P=" << P << " U=" << U
              << " S=" << S << " W=" << W << " L=" << L << " Rn=R" << std::dec
              << rn << std::hex << " (0x" << registers[rn] << ")"
              << " RegList=0x" << regList << " Mode=0x" << (cpsr & 0x1F)
              << std::dec << std::endl;
  }

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
  if (L && ((regList >> 15) & 1)) {
    Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                              "LDM setup: instr=0x%08x Rn=R%u base=0x%08x "
                              "start=0x%08x P=%d U=%d W=%d S=%d regList=0x%04x",
                              instruction, (unsigned)rn, base, startAddress,
                              (int)P, (int)U, (int)W, (int)S,
                              (unsigned)regList);
  }

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
          val &= ~1u;
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
          if (i == 8) {
            TraceR8Write(currentInstrAddr, currentInstrThumb,
                         currentInstrThumb ? (uint32_t)currentOp16
                                           : currentOp32,
                         old, registers[i]);
          }
          if (i == 11) {
            {
              const uint32_t tracedInstr =
                  currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
              TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr,
                            old, registers[i]);
              RecordR11Write(currentInstrAddr, currentInstrThumb, tracedInstr,
                             old, registers[i]);
            }
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

        // DEBUG: Trace ARM STM to 0x3001500
        if ((currentAddr >> 24) == 0x03 && (currentAddr & 0x7FFF) >= 0x1500 &&
            (currentAddr & 0x7FFF) < 0x1510) {
          std::cout << "[ARM STM] PC=0x" << std::hex << (registers[15] - 8)
                    << " [0x" << currentAddr << "] <- R" << std::dec << i
                    << " (0x" << std::hex << val << ")" << std::endl;
        }

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
        {
          const uint32_t tracedInstr =
              currentInstrThumb ? (uint32_t)currentOp16 : currentOp32;
          TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                        registers[rn]);
          RecordR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                         registers[rn]);
        }
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
      uint16_t value = memory.Read16(targetAddr);
      // Log LDRH at 0x188 specifically
      if (registers[15] == 0x190) { // PC is already +8 when we execute
        std::cerr << "[LDRH at 0x188] targetAddr=0x" << std::hex << targetAddr
                  << " value=0x" << value << " rd=" << rd
                  << " Current PC after fetch=0x" << registers[15] << std::dec
                  << std::endl;
      }
      registers[rd] = value;
    } else if (opcode == 2) { // LDRSB (Signed Byte)
      int8_t val = (int8_t)memory.Read8(targetAddr);
      registers[rd] = (int32_t)val;
    } else if (opcode == 3) { // LDRSH (Signed Halfword)
      int16_t val = (int16_t)memory.Read16(targetAddr);
      registers[rd] = (int32_t)val;
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
      // No handler installed; just return.
      break;
    }

    IrqContext ctx{};
    ctx.r0 = registers[0];
    ctx.r1 = registers[1];
    ctx.r2 = registers[2];
    ctx.r3 = registers[3];
    ctx.r12 = registers[12];
    ctx.lr = registers[14];
    ctx.pc = registers[15];
    ctx.cpsr = cpsr;
    ctx.thumbMode = thumbMode;
    irqStack.push_back(ctx);

    // Return point after handler completes.
    registers[14] = 0x000001A0;

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
      // Condition not met. Enable interrupts and halt until an IRQ happens.
      memory.Write16(IORegs::REG_IME, 1);
      // BIOS IntrWait must ensure the requested IRQ sources are enabled.
      // Games that direct-call BIOS (common in DirectBoot) may rely on BIOS
      // to OR waitFlags into IE, same as the SWI 0x04 implementation.
      uint16_t ie = memory.Read16(IORegs::REG_IE);
      memory.Write16(IORegs::REG_IE, (uint16_t)(ie | (waitFlags & 0xFFFF)));
      cpsr &= ~0x80; // Clear CPSR I-bit (enable IRQ)
      halted = true;

      // Re-run the BIOS call after waking.
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
}

void ARM7TDMI::ExecuteSWI(uint32_t comment) {
  // A reasonable baseline for SWI entry/exit + minimal BIOS wrapper work.
  // When we HLE a BIOS routine, we skip the real instruction stream, so we
  // must charge some time explicitly.
  constexpr int kSwiOverheadCycles = 32;

  switch (comment) {
  case 0x00: // SoftReset
    registers[15] = 0x08000000;
    registers[13] = 0x03007F00; // Reset SP
    thumbMode = false;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, false);
    break;
  case 0x01: // RegisterRamReset - Clear/Initialize RAM and registers
  {
    uint8_t flags = registers[0] & 0xFF;

    // Bit 0: Clear 256K EWRAM (0x02000000-0x0203FFFF)
    if (flags & 0x01) {
      for (uint32_t addr = 0x02000000; addr < 0x02040000; addr++) {
        memory.Write8(addr, 0);
      }
    }

    // Bit 1: Clear 32K IWRAM (0x03000000-0x03007FFF, excluding last 0x200
    // bytes)
    if (flags & 0x02) {
      for (uint32_t addr = 0x03000000; addr < 0x03007E00; addr++) {
        memory.Write8(addr, 0);
      }
    }

    // Bit 2: Clear Palette RAM (0x05000000-0x050003FF)
    if (flags & 0x04) {
      for (uint32_t addr = 0x05000000; addr < 0x05000400; addr++) {
        memory.Write8(addr, 0);
      }
    }

    // Bit 3: Clear VRAM (0x06000000-0x06017FFF)
    if (flags & 0x08) {
      for (uint32_t addr = 0x06000000; addr < 0x06018000; addr++) {
        memory.Write8(addr, 0);
      }
    }

    // Bit 4: Clear OAM (0x07000000-0x070003FF)
    if (flags & 0x10) {
      for (uint32_t addr = 0x07000000; addr < 0x07000400; addr++) {
        memory.Write8(addr, 0);
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
    halted = true;
    sleepHalt = true;
    debuggerHalt = false;
    break;
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
      TraceWatchWrite32(
          currentInstrAddr, currentInstrThumb,
          currentInstrThumb ? (uint32_t)currentOp16 : currentOp32, 0x03007FF8,
          (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)), registers[13]);
      memory.Write16(0x03007FF8,
                     (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)));
      registers[0] =
          0; // Clear the "Clear Old" flag so we don't do it again on resume
    }

    uint16_t currentFlags = memory.Read16(0x03007FF8);
    if (currentFlags & (waitFlags & 0xFFFF)) {
      // Condition met! Return.
      // Acknowledge flags in BIOS_IF (Standard BIOS behavior)
      TraceWatchWrite32(
          currentInstrAddr, currentInstrThumb,
          currentInstrThumb ? (uint32_t)currentOp16 : currentOp32, 0x03007FF8,
          (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)), registers[13]);
      memory.Write16(0x03007FF8,
                     (uint16_t)(currentFlags & ~(waitFlags & 0xFFFF)));
      return;
    }

    // Condition not met. Enable interrupts and Halt.
    memory.Write16(IORegs::REG_IME, 1); // IME=1
    uint16_t ie = memory.Read16(IORegs::REG_IE);
    memory.Write16(IORegs::REG_IE,
                   ie | (waitFlags & 0xFFFF)); // Enable required IRQs
    cpsr &= ~0x80;                             // Enable IRQ in CPSR
    halted = true;
    sleepHalt = true;
    debuggerHalt = false;

    // Rewind PC to re-execute this SWI instruction
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

    // Debug: trace CpuSet to VRAM
    // DISABLED: Too verbose during boot
    // if ((dst & 0xFF000000) == 0x06000000) {
    //     std::cout << "[SWI 0x0B] CpuSet to VRAM: src=0x" << std::hex << src
    //               << " dst=0x" << dst << " len=" << std::dec << len
    //               << " 32bit=" << is32Bit << " fixed=" << fixedSrc <<
    //               std::endl;
    // }

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

    // Debug: trace CpuFastSet to VRAM
    // DISABLED: Too verbose during boot
    // if ((dst & 0xFF000000) == 0x06000000) {
    //     std::cout << "[SWI 0x0C] CpuFastSet to VRAM: src=0x" << std::hex <<
    //     src
    //               << " dst=0x" << dst << " len=" << std::dec << len
    //               << " fixed=" << fixedSrc << std::endl;
    // }

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

    for (uint32_t i = 0; i < srcLen;) {
      if (srcBitPos == 0) {
        srcByte = memory.Read8(src++);
        i++;
      }

      uint32_t val = (srcByte >> srcBitPos) & srcMask;
      srcBitPos += srcWidth;
      if (srcBitPos >= 8)
        srcBitPos = 0;

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

    // Read header: bits 4-7 = compression type (1 = LZ77), bits 8-31 =
    // decompressed size
    uint32_t header = memory.Read32(src);
    uint32_t decompSize = header >> 8;

    // Debug: trace LZ77 to VRAM
    if (toVram && (dst & 0xFF000000) == 0x06000000) {
      std::cout << "[SWI 0x12] LZ77 to VRAM: src=0x" << std::hex << src
                << " dst=0x" << dst << " size=" << std::dec << decompSize
                << std::endl;
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

    const uint32_t old = registers[rd];
    registers[rd] = res;
    if (rd == 8) {
      TraceR8Write(currentInstrAddr, currentInstrThumb, (uint32_t)currentOp16,
                   old, registers[rd]);
    }

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
    const uint32_t old = registers[rd];
    registers[rd] = res;
    if (rd == 8) {
      TraceR8Write(currentInstrAddr, currentInstrThumb, (uint32_t)currentOp16,
                   old, registers[rd]);
    }

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
      const uint32_t old = registers[rd];
      registers[rd] = imm;
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb, (uint32_t)currentOp16,
                     old, registers[rd]);
      }
      UpdateNZFlags(cpsr, registers[rd]);
    } else if (opcode == 1) { // CMP Rd, #Offset8
      uint32_t result = registers[rd] - imm;
      UpdateNZFlags(cpsr, result);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(registers[rd], imm));
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectSubOverflow(registers[rd], imm, result));
    } else if (opcode == 2) { // ADD Rd, #Offset8
      uint32_t val = registers[rd];
      const uint32_t old = registers[rd];
      registers[rd] = val + imm;
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb, (uint32_t)currentOp16,
                     old, registers[rd]);
      }
      UpdateNZFlags(cpsr, registers[rd]);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, DetectAddCarry(val, imm));
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectAddOverflow(val, imm, registers[rd]));
    } else if (opcode == 3) { // SUB Rd, #Offset8
      uint32_t val = registers[rd];
      const uint32_t old = registers[rd];
      registers[rd] = val - imm;
      if (rd == 8) {
        TraceR8Write(currentInstrAddr, currentInstrThumb, (uint32_t)currentOp16,
                     old, registers[rd]);
      }
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
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    case 0x1: // EOR
      res = val ^ op2;
      UpdateNZFlags(cpsr, res);
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    case 0x2: // LSL
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = LogicalShiftLeft(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    }
    case 0x3: // LSR
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = LogicalShiftRight(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    }
    case 0x4: // ASR
    {
      uint32_t shiftAmount = op2 & 0xFF;
      uint32_t tempCPSR = cpsr;
      res = ArithmeticShiftRight(val, shiftAmount, tempCPSR, true);
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, CarryFlagSet(tempCPSR));
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
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
      SetCPSRFlag(cpsr, CPSR::FLAG_V,
                  DetectAddOverflow(val, op2 + carryIn, res));
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    }
    case 0x6: // SBC
    {
      bool carryIn = CarryFlagSet(cpsr);
      uint32_t operand = op2 + static_cast<uint32_t>(!carryIn);
      res = val - operand;
      UpdateNZFlags(cpsr, res);
      SetCPSRFlag(cpsr, CPSR::FLAG_C, !DetectSubBorrow(val, operand));
      SetCPSRFlag(cpsr, CPSR::FLAG_V, DetectSubOverflow(val, operand, res));
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
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
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    }
    case 0x8: // TST
      res = val & op2;
      UpdateNZFlags(cpsr, res);
      break;
    case 0x9: // NEG (RSB Rd, Rs, #0)
      res = 0 - op2;
      UpdateNZFlags(cpsr, res);
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
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
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    case 0xD: // MUL
      res = val * op2;
      UpdateNZFlags(cpsr, res);
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    case 0xE: // BIC
      res = val & (~op2);
      UpdateNZFlags(cpsr, res);
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
      break;
    case 0xF: // MVN
      res = ~op2;
      UpdateNZFlags(cpsr, res);
      {
        const uint32_t old = registers[rd];
        registers[rd] = res;
        if (rd == 8)
          TraceR8Write(currentInstrAddr, currentInstrThumb,
                       (uint32_t)currentOp16, old, registers[rd]);
      }
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
      // PC as source needs +2 adjustment in Thumb mode
      uint32_t rmVal = (regM == 15) ? pcValue : registers[regM];
      if (regD == 15) {
        // For ADD PC, Rm: PC is destination, use current registers[15]
        uint32_t newPC = registers[15] + rmVal;
        if (newPC >= 0x08125C00 && newPC < 0x08127000) {
          Logger::Instance().LogFmt(
              LogLevel::Error, "CPU",
              "ADD PC instruction: PC=0x%08x Rm=%d RmVal=0x%08x newPC=0x%08x",
              registers[15] - 2, regM, rmVal, newPC);
        }
        LogBranch(registers[15] - 2, newPC);
        // In Thumb, ADD to PC should clear bit 0 (PC must be aligned)
        registers[15] = newPC & 0xFFFFFFFE;
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
        if (val >= 0x08125C00 && val < 0x08127000) {
          Logger::Instance().LogFmt(
              LogLevel::Error, "CPU",
              "MOV PC instruction: PC=0x%08x Rm=%d val=0x%08x",
              registers[15] - 2, regM, val);
        }
        LogBranch(registers[15] - 2, val);
        // In Thumb, MOV PC, Rm should clear bit 0 (not interworking on
        // ARM7TDMI)
        registers[15] = val & 0xFFFFFFFE;
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
          {
            const uint32_t tracedInstr = (uint32_t)currentOp16;
            TraceR11Write(currentInstrAddr, currentInstrThumb, tracedInstr, old,
                          registers[regD]);
            RecordR11Write(currentInstrAddr, currentInstrThumb, tracedInstr,
                           old, registers[regD]);
          }
        }
      }
    } else if (opcode == 3) { // BX Rm
      uint32_t target = registers[regM];

      const bool traceEarlyBx = EnvFlagCached("AIO_TRACE_EARLY_BX");
      if (traceEarlyBx) {
        const uint32_t fromPC = currentInstrAddr;
        const uint32_t masked = target & 0xFFFFFFFEu;
        const bool entersArm = ((target & 1u) == 0u);
        if (entersArm && fromPC >= 0x08001000u && fromPC <= 0x08002000u) {
          Logger::Instance().LogFmt(
              LogLevel::Error, "CPU",
              "EARLY THUMB BX->ARM fromPC=0x%08x instr=0x%04x Rm=%u "
              "target=0x%08x masked=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x",
              fromPC, instruction, (unsigned)regM, target, masked,
              registers[14], registers[13], cpsr);
        }
      }

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

      /*
      if (target >= 0x03000000 && target < 0x04000000 && (target & 1) == 0) {
           // Hack: Force Thumb mode for even IWRAM target (Fixes SMA2 crash)
           target |= 1;
      }
      */

      if (target >= 0x08125C00 && target < 0x08127000) {
        Logger::Instance().LogFmt(
            LogLevel::Error, "CPU",
            "BX instruction: PC=0x%08x Rm=%d target=0x%08x", registers[15] - 2,
            regM, target);
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
        const uint32_t val = memory.Read32(addr);
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
        int16_t val = (int16_t)memory.Read16(addr);
        registers[rd] = (int32_t)val;
      } else { // LDRSB
        int8_t val = (int8_t)memory.Read8(addr);
        registers[rd] = (int32_t)val;
      }
    } else {
      if (H) { // LDRH
        registers[rd] = memory.Read16(addr);
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
      } else
        registers[rd] = memory.Read32(addr);
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
      registers[rd] = memory.Read16(addr);
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

    // DEBUG: Trace STMIA to DMA2 control registers (0x40000C8-0x40000D3) -
    // disabled bool traceDMA2 = (!L && addr >= 0x40000C8 && addr <= 0x40000D3);
    // if (traceDMA2) {
    //     std::cout << "[STMIA DMA2] PC=0x" << std::hex << (registers[15] - 2)
    //               << " R" << std::dec << rb << "=0x" << std::hex << addr
    //               << " rList=0x" << (int)rList;
    //     for (int i=0; i<8; ++i) {
    //         if ((rList >> i) & 1) {
    //             std::cout << " R" << i << "=0x" << registers[i];
    //         }
    //     }
    //     std::cout << std::dec << std::endl;
    // }

    // DEBUG: Trace STMIA to 0x3001500 - disabled
    // bool traceMixbuf = (!L && (addr & 0xFF000000) == 0x03000000 && (addr &
    // 0x7FFF) >= 0x1500 && (addr & 0x7FFF) < 0x1600); if (traceMixbuf) {
    //     std::cout << "[STMIA] PC=0x" << std::hex << (registers[15] - 2)
    //               << " R" << std::dec << rb << "=0x" << std::hex << addr
    //               << " rList=0x" << (int)rList << std::dec << std::endl;
    // }

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
    // if (traceMixbuf) {
    //     std::cout << "  Writeback R" << std::dec << rb << ": 0x" << std::hex
    //     << startAddr
    //               << " -> 0x" << addr << std::dec << std::endl;
    // }
    registers[rb] = addr;
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

      // DEBUG: Log branches in EEPROM loop
      // Disabled: EEPROM branch logging
      // static int branchCount = 0;
      // if (registers[15] >= 0x0809E1CC && registers[15] <= 0x0809E1F0) {
      //     if (branchCount++ % 50 == 0) {
      //         std::cerr << "[EEPROM BRANCH #" << branchCount << "] PC=0x" <<
      //         std::hex << registers[15]
      //                   << " cond=" << cond << " satisfied=" << condSatisfied
      //                   << " offset=" << (int)offset
      //                   << " Z=" << ((cpsr & CPSR::FLAG_Z) ? 1 : 0)
      //                   << std::dec << std::endl;
      //     }
      // }

      if (condSatisfied) {
        // registers[15] points at the next instruction (instrAddr+2).
        // Branch base should be PC+4 (instrAddr+4) which equals
        // registers[15]+2.
        uint32_t target = registers[15] + 2 + (offset * 2);
        LogBranch(registers[15] - 2, target);
        registers[15] = target;
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
    if (target >= 0x08125C00 && target < 0x08127000) {
      Logger::Instance().LogFmt(LogLevel::Error, "CPU",
                                "B (unconditional) instruction: PC=0x%08x "
                                "instr=0x%04x offset=%d target=0x%08x",
                                registers[15] - 2, instruction, offset, target);
    }
    LogBranch(registers[15] - 2, target);
    registers[15] = target;
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
