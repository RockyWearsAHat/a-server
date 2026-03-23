#include "emulator/ps1/PS1HleBios.h"
#include "emulator/common/Logger.h"
#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/PS1.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/R3000A.h"
#include <cstdlib>
#include <cstring>

namespace AIO::Emulator::PS1 {

namespace {

bool IsPs1HleTraceEnabled() {
  const char *value = std::getenv("AIO_TRACE_PS1_HLE");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

} // namespace

// ─── Public Entry Point ─────────────────────────────────────────────────

bool PS1HleBios::InitHLE(PS1 &ps1) {
  auto &memory = ps1.GetMemory();
  auto &cpu = ps1.GetCPU();
  auto &gpu = ps1.GetGPU();
  auto &cdrom = ps1.GetCDROM();

  ResetState();

  memoryPtr = &memory;

  // Real BIOS boots with 0x0B88 (8MB mirroring). Most games never change this.
  // Games that need 2MB-only mode call A(9Fh) SetMem(2) explicitly.
  memory.Write32(0x1F801060, 0x00000B88);

  // Zero all RAM before setting up kernel state or loading the EXE.
  // The real BIOS clears RAM during boot. Many games (including Crash
  // Bandicoot) have BSS globals beyond the loaded .text section that must
  // start at zero but have no memfill header entry to zero them.
  std::memset(memory.GetRAMPointer(), 0, MemSize::RAM);

  PopulateBiosRegion(memory);
  InstallKernelStubs(memory);
  InstallTrampolines(memory);
  InitKernelState(memory);
  InitGPU(gpu);
  InitCDROM(cdrom);
  cdromHandlersRegistered = true;

  // Real BIOS configures DICR during boot to enable per-channel DMA IRQs.
  // Games rely on DMA completion events (class 0xF1nnnnnn) being delivered.
  auto &dma = ps1.GetDMA();
  dma.Write32(IO::DMA_DICR, 0x00FF0000);

  // Real BIOS enables VBlank IRQ in I_MASK during SysInit before transferring
  // control to the game. Without this, HasPendingIRQ() is always false and the
  // CPU never takes the VBlank exception, causing VSync() to timeout forever.
  ps1.GetInterrupts().WriteMask(IRQ::VBLANK);

  if (!FindAndLoadExe(memory, cdrom, cpu, gpu)) {
    return false;
  }

  return true;
}

void PS1HleBios::ResetState() {
  events = {};
  interruptsEnabled = true;
  hookedEntryIntHandler = 0;
  std::memset(&savedFrame, 0, sizeof(savedFrame));
  heapBase = 0;
  heapSize = 0;
  heapPtr = 0;
  kernelHeapBase = 0;
  kernelHeapSize = 0;
  kernelHeapPtr = 0;
  handlersArrayAddr = 0;
  hleSeed = 0;
  changeClearRCntFlags = {1, 1, 1, 1};
  cdromHandlersRegistered = false;
  b0TableRamAddr = 0;
  c0TableRamAddr = 0;
  inExceptionHandler = false;
  memoryPtr = nullptr;
  pendingCallbacks.clear();
  callbackPS1 = nullptr;
  padBuf1Addr = 0;
  padBuf1Size = 0;
  padBuf2Addr = 0;
  padBuf2Size = 0;
  padStarted = false;
}

// ─── HLE Exception Handler ──────────────────────────────────────────────

void PS1HleBios::HandleException(PS1 &ps1) {
  auto &cpu = ps1.GetCPU();
  auto &mem = ps1.GetMemory();
  auto &irqs = ps1.GetInterrupts();

  uint32_t cause = cpu.GetCause();
  uint32_t excCode = (cause >> 2) & 0x1F;

  if (IsPs1HleTraceEnabled()) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    uint32_t istat = irqs.ReadStat();
    uint32_t imask = irqs.ReadMask();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "HandleException: excCode=%u EPC=0x%08X cause=0x%08X "
               "PC=0x%08X SR=0x%08X inExc=%d istat=0x%04X imask=0x%04X "
               "pending=0x%04X",
               excCode, cpu.GetEPC(), cause, cpu.GetPC(),
               cpu.GetCOP0(CPU::COP0::SR), inExceptionHandler ? 1 : 0, istat,
               imask, istat & imask);
  }

  // ── Nested exception guard ──────────────────────────────────────────
  // The real PSX kernel doesn't support nested exceptions. If any exception
  // fires while a longjmp/handler-chain handler is still executing (before
  // it calls ReturnFromException), we must NOT overwrite the TCB or
  // re-dispatch the longjmp. Return to EPC with RFE so the handler can
  // finish and call ReturnFromException itself.
  if (inExceptionHandler && excCode != 0x08) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    uint32_t epc = cpu.GetEPC();
    if (IsPs1HleTraceEnabled()) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "HandleException: nested guard RFE → EPC=0x%08X SR=0x%08X "
                 "PC=0x%08X excCode=%u",
                 epc, cpu.GetCOP0(CPU::COP0::SR), cpu.GetPC(), excCode);
    }
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(epc);
    return;
  }

  // Save full CPU state to the current TCB for hardware interrupts and
  // non-SYSCALL exceptions. SYSCALL exceptions (used by HLE BIOS
  // trampolines) must NOT overwrite the TCB — ReturnFromException reads
  // the TCB to restore state, and a SYSCALL from within exception context
  // would corrupt the saved EPC.
  uint32_t tcbPtr = mem.ReadRAM32(PCB_ADDR) & 0x1FFFFF;
  savedTcbPtr = tcbPtr;
  if (excCode != 0x08) {
    uint32_t newEpc = cpu.GetEPC();
    for (int i = 0; i < 32; i++) {
      savedRegs[i] = cpu.GetRegister(i);
      mem.WriteRAM32(tcbPtr + 0x08 + i * 4, savedRegs[i]);
    }
    savedEpc = newEpc;
    savedHI = cpu.GetHI();
    savedLO = cpu.GetLO();
    savedSR = cpu.GetCOP0(CPU::COP0::SR);
    mem.WriteRAM32(tcbPtr + 0x88, newEpc);
    mem.WriteRAM32(tcbPtr + 0x8C, savedHI);
    mem.WriteRAM32(tcbPtr + 0x90, savedLO);
    mem.WriteRAM32(tcbPtr + 0x94, savedSR);
    mem.WriteRAM32(tcbPtr + 0x98, cause);
  }

  if (excCode == 0 && hookedEntryIntHandler != 0) {
    uint32_t istat = irqs.ReadStat();
    uint32_t imask = irqs.ReadMask();
    uint32_t pending = istat & imask;

    // Deliver BIOS events and ack IRQs, simulating the real BIOS
    // handler chain (priority 0: CDROM, priority 1: timers/VBlank/IRQs).
    // On real hardware each handler acks its IRQ and may call
    // ReturnFromException. The HLE delivers events inline and tracks
    // which IRQs were fully processed for acking below.
    uint32_t deliveredIrqs = 0;

    if (pending & IRQ::VBLANK) {
      DeliverEvent(0xF0000001, 0x1000);
      DeliverEvent(0xF2000003, 0x0002);

      // Fire all SDK VBlank events (class 0xF0000011 = libgpu GPU/VSync events).
      // On real hardware, libgpu registers a SysEnqIntRP VBlank handler that
      // fires each registered F0000011h slot on every VBlank. Since our HLE
      // does not run the native handler chain, we deliver all enabled F0000011h
      // events directly. Games using libgpu VSync() depend on this delivery.
      DeliverEventClass(0xF0000011);
      // Also fire F0000009/0x0020 events polled as secondary VBlank signal.
      DeliverEvent(0xF0000009, 0x0020);

      // HLE pad polling: write controller state to game's pad buffers.
      // Real BIOS priority-2 PadCard handler does SIO transactions here.
      if (padStarted) {
        auto &ctrl = ps1.GetController();
        uint16_t buttons = ctrl.GetButtonState();

        if (padBuf1Addr != 0 && padBuf1Size >= 4) {
          mem.Write8(padBuf1Addr + 0, 0x00); // status OK
          mem.Write8(padBuf1Addr + 1, 0x41); // digital pad ID
          mem.Write8(padBuf1Addr + 2, buttons & 0xFF);
          mem.Write8(padBuf1Addr + 3, (buttons >> 8) & 0xFF);
        }
        if (padBuf2Addr != 0 && padBuf2Size >= 4) {
          mem.Write8(padBuf2Addr + 0, 0xFF); // no controller
          mem.Write8(padBuf2Addr + 1, 0xFF);
          mem.Write8(padBuf2Addr + 2, 0xFF);
          mem.Write8(padBuf2Addr + 3, 0xFF);
        }
      }

      if (changeClearRCntFlags[3] != 0)
        deliveredIrqs |= IRQ::VBLANK;
    }
    if (pending & IRQ::TIMER0) {
      DeliverEvent(0xF0000005, 0x0002);
      DeliverEvent(0xF2000000, 0x0002);
      if (changeClearRCntFlags[0] != 0)
        deliveredIrqs |= IRQ::TIMER0;
    }
    if (pending & IRQ::TIMER1) {
      DeliverEvent(0xF0000006, 0x0002);
      DeliverEvent(0xF2000001, 0x0002);
      if (changeClearRCntFlags[1] != 0)
        deliveredIrqs |= IRQ::TIMER1;
    }
    if (pending & IRQ::TIMER2) {
      // Always deliver Timer2 events — the old IsCDBusy() guard was added to
      // prevent Timer2 from interrupting in-flight CD decompression on
      // real hardware (I-cache eviction risk), but our emulator has no
      // I-cache simulation.  Suppressing Timer2 during CD streaming was
      // inadvertently blocking animation timers in games like Crash
      // Bandicoot that stream continuously from the CD.
      DeliverEvent(0xF0000007, 0x0002);
      DeliverEvent(0xF2000002, 0x0002);
      if (changeClearRCntFlags[2] != 0)
        deliveredIrqs |= IRQ::TIMER2;
    }
    if (pending & IRQ::DMA) {
      DeliverEvent(0xF0000004, 0x1000);

      auto &dma = ps1.GetDMA();
      uint32_t dicrVal = dma.GetDICR();
      uint32_t completedFlags = (dicrVal >> 24) & 0x7F;
      for (uint32_t ch = 0; ch < 7; ch++) {
        if (completedFlags & (1 << ch)) {
          DeliverEvent(0xF1000000 | ch, 0x0002);
        }
      }
      if (completedFlags != 0) {
        dma.AcknowledgeDICRFlags(completedFlags);
      }
      deliveredIrqs |= IRQ::DMA;
    }
    if (pending & IRQ::CDROM) {
      if (cdromHandlersRegistered) {
        // BIOS CDROM state-machine is active (via enqueueCDRomHandlers).
        // Process the interrupt inline: deliver events and ack.
        auto &cdrom = ps1.GetCDROM();
        uint8_t intType = cdrom.GetInterruptFlag();

        uint32_t spec = 0;
        switch (intType) {
        case 1:
          spec = 0x0040;
          break;
        case 2:
          spec = 0x0020;
          break;
        case 3:
          spec = 0x0010;
          break;
        case 4:
          spec = 0x0080;
          break;
        case 5:
          spec = 0x8000;
          break;
        default:
          break;
        }
        if (spec != 0) {
          DeliverEvent(0xF0000003, spec);
        }
        DeliverEvent(0xF0000003, 0x1000);
        cdrom.AcknowledgeInterrupt();
        deliveredIrqs |= IRQ::CDROM;
      }
      // When cdromHandlersRegistered is false, the game handles CDROM
      // interrupts in its own exception handler (via HookEntryInt longjmp).
      // Leave the IRQ pending so the longjmp path runs.
    }
    if (pending & IRQ::SPU) {
      DeliverEvent(0xF0000009, 0x1000);
      deliveredIrqs |= IRQ::SPU;
    }

    // Ack all IRQs that were fully processed above, matching the real
    // BIOS handler chain behavior where each handler acks its IRQ.
    if (deliveredIrqs != 0) {
      irqs.ClearIRQ(deliveredIrqs);
      pending = irqs.ReadStat() & imask;
    }

    // TEMP DIAG
    if (IsPs1HleTraceEnabled()) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Warning, "PS1.HLE",
                 "POST-ACK: deliveredIrqs=0x%04X istat=0x%04X imask=0x%04X "
                 "pending=0x%04X callbacks=%zu cdromReg=%d",
                 deliveredIrqs, irqs.ReadStat(), irqs.ReadMask(), pending,
                 pendingCallbacks.size(), cdromHandlersRegistered ? 1 : 0);
    }

    // ── Dispatch mode=0x1000 callbacks ────────────────────────────────
    // If any DeliverEvent call queued a mode=0x1000 callback, dispatch
    // it now. The CPU will execute the callback and return via the
    // CALLBACK_RETURN trampoline, which re-enters exception handling
    // at the priority 1 / longjmp phase.
    if (!pendingCallbacks.empty()) {
      callbackPS1 = &ps1;
      uint32_t cbAddr = pendingCallbacks.front();
      pendingCallbacks.erase(pendingCallbacks.begin());

      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Debug, "PS1.HLE",
                 "Dispatching callback 0x%08X (%zu remaining)", cbAddr,
                 pendingCallbacks.size());

      inExceptionHandler = true;
      uint32_t trampoline = 0x80000000 | CALLBACK_RETURN_ADDR;
      cpu.SetRegister(31, trampoline); // $ra = callback return trampoline
      cpu.SetPC(cbAddr);
      return;
    }

    // All events delivered and all IRQs acked — fast return to game.
    if (pending == 0) {
      for (int i = 1; i < 32; i++) {
        if (i == 26)
          continue;
        cpu.SetRegister(i, savedRegs[i]);
      }
      uint32_t sr = (savedSR & ~0xF) | ((savedSR >> 2) & 0xF);
      cpu.SetHI(savedHI);
      cpu.SetLO(savedLO);
      cpu.SetCOP0(CPU::COP0::SR, sr);
      cpu.SetPC(savedEpc);
      inExceptionHandler = false;

      if (IsPs1HleTraceEnabled()) {
        auto &log = AIO::Emulator::Common::Logger::Instance();
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                   "HandleException: hooked-path fast RFE epc=0x%08X "
                   "sr=0x%08X ackedIrqs=0x%04X",
                   savedEpc, sr, deliveredIrqs);
      }
      return;
    }

    // ── BIOS Priority 1+2: Timer / VBlank / Pad handler simulation ──
    // Real PS1 BIOS priority chain for IRQs:
    //   Priority 0: CDROM DMA, CDROM IO, SYSCALL
    //   Priority 1: VBlank, Timer0-2 (each has verifier + handler)
    //   Priority 2: PadCard (sio0Handler) — acks VBlank, reads pads
    //   Priority 3: DefInt — delivers generic events (F0000001h etc.)
    //
    // changeClearRCntFlags[n]==1 (default): ack IRQ + ReturnFromException.
    // changeClearRCntFlags[n]==0: don't ack, fall through to longjmp handler.
    {
      struct RCntEntry {
        uint32_t irqBit;
        int flagIndex;
      };
      static constexpr RCntEntry rcntTable[] = {
          {IRQ::TIMER0, 0},
          {IRQ::TIMER1, 1},
          {IRQ::TIMER2, 2},
          {IRQ::VBLANK, 3},
      };

      uint32_t ackedBits = 0;
      for (auto &rc : rcntTable) {
        if (!(pending & rc.irqBit))
          continue;
        if (changeClearRCntFlags[rc.flagIndex] != 0) {
          ackedBits |= rc.irqBit;
        }
      }

      if (ackedBits != 0) {
        irqs.ClearIRQ(ackedBits);
        pending = irqs.ReadStat() & imask;

        if (pending == 0) {
          uint32_t epc = cpu.GetEPC();
          uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);

          if (IsPs1HleTraceEnabled()) {
            auto &log = AIO::Emulator::Common::Logger::Instance();
            log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                       "HandleException: quick-RFE epc=0x%08X ra=0x%08X "
                       "sr=0x%08X ackedBits=0x%04X",
                       epc, cpu.GetRegister(31), sr, ackedBits);
          }

          sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
          cpu.SetCOP0(CPU::COP0::SR, sr);
          cpu.SetPC(epc);
          return;
        }
      }
    }

    // Fall through to handler chain / longjmp dispatch below
  }

  // ── Non-interrupt exceptions (RI, CpU, AdEL, BP, etc.) ─────────────
  // On real PS1 BIOS, ALL exceptions (not just IRQs) go through the
  // exception handler chain. The game's HookEntryInt handler receives
  // every exception and decides how to handle it (checking CAUSE in TCB).
  // We skip IRQ event delivery for non-interrupt exceptions but still
  // dispatch to the handler chain / longjmp handler.
  if (excCode != 0 && excCode != 0x08 && hookedEntryIntHandler != 0) {
    if (IsPs1HleTraceEnabled()) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "HandleException: non-IRQ exception excCode=%u "
                 "EPC=0x%08X → dispatching to handler chain",
                 excCode, cpu.GetEPC());
    }
    // Fall through to handler chain / longjmp dispatch below
  }

  // ── Handler chain + longjmp dispatch (shared for all exception types) ──
  if (hookedEntryIntHandler != 0 && excCode != 0x08) {
    // Walk the SysEnqIntRP handler chain (priority 0 = IRQ handlers).
    if (handlersArrayAddr != 0) {
      uint32_t chainHead =
          mem.Read32(0x80000000 | handlersArrayAddr); // priority 0
      uint32_t entry = chainHead;
      while (entry != 0) {
        uint32_t func2 = mem.Read32(entry + 0x04);
        if (func2 != 0) {
          inExceptionHandler = true;
          uint32_t rfeTrampoline =
              0x80000000 | (B0_TRAMPOLINE_ADDR + 0x17 * 12);
          cpu.SetRegister(31, rfeTrampoline);
          cpu.SetPC(func2);
          return;
        }
        entry = mem.Read32(entry + 0x00);
      }
    }

    // Fallback: longjmp into the setjmp buffer registered via HookEntryInt.
    {
      uint32_t sjBuf = hookedEntryIntHandler;
      uint32_t ra = mem.Read32(sjBuf + 0x00);
      uint32_t sp = mem.Read32(sjBuf + 0x04);

      if (IsPs1HleTraceEnabled()) {
        auto &log = AIO::Emulator::Common::Logger::Instance();
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                   "HandleException: longjmp dispatch excCode=%u "
                   "ra=0x%08X sp=0x%08X savedEpc=0x%08X pending=0x%04X",
                   excCode, ra, sp, savedEpc,
                   irqs.ReadStat() & irqs.ReadMask());
      }

      // longjmp transfers control back to game code — exception handling is
      // complete. Clear the guard so future exceptions are handled normally.
      inExceptionHandler = false;
      cpu.SetRegister(29, sp);
      cpu.SetRegister(30, mem.Read32(sjBuf + 0x08)); // FP
      for (int i = 0; i < 8; i++) {
        cpu.SetRegister(16 + i, mem.Read32(sjBuf + 0x0C + i * 4)); // s0-s7
      }
      cpu.SetRegister(28, mem.Read32(sjBuf + 0x2C)); // GP
      cpu.SetRegister(2, 1);                         // $v0 = 1
      cpu.SetPC(ra);
      return;
    }
  }

  if (excCode == 0) {
    // Hardware interrupt but no registered handler — acknowledge and return
    uint32_t istat = irqs.ReadStat();
    uint32_t imask = irqs.ReadMask();
    uint32_t pending = istat & imask;
    if (pending & IRQ::VBLANK) {
      DeliverEvent(0xF0000001, 0x0001);
      irqs.WriteStat(istat & ~IRQ::VBLANK);
    }

    uint32_t epc = cpu.GetEPC();
    cpu.SetPC(epc);
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    return;
  }

  if (excCode == 0x08) {
    // SYSCALL — dispatch based on r4 (function number)
    uint32_t epc = cpu.GetEPC();
    uint32_t r4 = cpu.GetRegister(4);
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);

    if (r4 == 0x01) {
      // SYS(01h) EnterCriticalSection: disable interrupts
      // Returns 1 if both SR bits 2 and 10 were set, else 0
      uint32_t bit2 = (sr >> 2) & 1;
      uint32_t bit10 = (sr >> 10) & 1;
      cpu.SetRegister(2, (bit2 && bit10) ? 1 : 0);
      sr &= ~((1 << 2) | (1 << 10));
    } else if (r4 == 0x02) {
      // SYS(02h) ExitCriticalSection: enable interrupts
      sr |= (1 << 2) | (1 << 10);
    } else if (r4 == 0x03) {
      // SYS(03h) ChangeThreadSubFunction: switch thread via r5
    } else {
      // SYS(04h..FFFFFFFFh): DeliverEvent(F0000010h, 4000h)
      DeliverEvent(0xF0000010, 0x4000);
    }

    // RFE: pop the SR interrupt stack (bits 5:0 shift right by 2)
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(epc + 4);
    return;
  }

  // Unresolved exception (not IRQ, not SYSCALL, not RI/CpU): AdEL,
  // AdES, BusError, Overflow, etc. Per PSX-SPX, the real BIOS delivers
  // DeliverEvent(F0000010h, 1000h) and then calls SystemError (hangs).
  // We log the error and halt the CPU to avoid infinite re-execution.
  {
    static const char *excNames[] = {
        "INT",     "MOD", "TLBL", "TLBS", "AdEL", "AdES", "IBE", "DBE",
        "SYSCALL", "BP",  "RI",   "CpU",  "Ov",   "TRAP", "?",   "FPE"};
    const char *name = (excCode < 16) ? excNames[excCode] : "?";
    uint32_t epc = cpu.GetEPC();
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
               "Unresolved exception %s (excCode=%u) at EPC=0x%08X "
               "cause=0x%08X — halting CPU",
               name, excCode, epc, cause);
    log.LogFmt(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
               "  ra=0x%08X sp=0x%08X v0=0x%08X a0=0x%08X", cpu.GetRegister(31),
               cpu.GetRegister(29), cpu.GetRegister(2), cpu.GetRegister(4));
    uint32_t physEpc = epc & 0x1FFFFF;
    if (physEpc + 32 <= 0x200000) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
                 "  insn @ EPC: %08X %08X %08X %08X %08X %08X %08X %08X",
                 mem.ReadRAM32(physEpc), mem.ReadRAM32(physEpc + 4),
                 mem.ReadRAM32(physEpc + 8), mem.ReadRAM32(physEpc + 12),
                 mem.ReadRAM32(physEpc + 16), mem.ReadRAM32(physEpc + 20),
                 mem.ReadRAM32(physEpc + 24), mem.ReadRAM32(physEpc + 28));
    }

    DeliverEvent(0xF0000010, 0x1000);

    // Halt the CPU — the real BIOS hangs forever via SystemError
    cpu.SetPC(epc);
    cpu.SetHalted(true);
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
  }
}

// ─── HLE BIOS Dispatch ─────────────────────────────────────────────────

bool PS1HleBios::Dispatch(PS1 &ps1, uint8_t table, uint8_t func) {
  auto &log = AIO::Emulator::Common::Logger::Instance();
  auto &cpu = ps1.GetCPU();
  auto &mem = ps1.GetMemory();

  // PSY-Q and other runtimes patch the A0/B0/C0 function tables in RAM
  // to redirect BIOS calls to their own implementations. Check if the
  // table entry still points to our trampoline; if not, redirect the CPU
  // to the patched target instead of running the HLE handler.
  uint32_t tableAddr = 0;
  uint32_t trampolineBase = 0;
  switch (table) {
  case 0xA0:
    tableAddr = A0_TABLE_ADDR;
    trampolineBase = A0_TRAMPOLINE_ADDR;
    break;
  case 0xB0:
    tableAddr = B0_TABLE_ADDR;
    trampolineBase = B0_TRAMPOLINE_ADDR;
    break;
  case 0xC0:
    tableAddr = C0_TABLE_ADDR;
    trampolineBase = C0_TRAMPOLINE_ADDR;
    break;
  }

  uint32_t expectedTrampoline = 0x80000000 | (trampolineBase + func * 12);
  uint32_t actualEntry = mem.Read32(0x80000000 | (tableAddr + func * 4));

  if (actualEntry != expectedTrampoline) {
    // The real BIOS stores kernel code in the first 64KB of RAM (relocated
    // from ROM). In HLE mode we never populate that region, so redirecting
    // to addresses in kernel RAM would execute garbage. RAM mirroring can
    // also corrupt table entries with decompressed data — reject obviously
    // invalid targets (unaligned, kernel RAM, or non-KSEG0 addresses).
    uint32_t physTarget = actualEntry & 0x1FFFFFFF;
    constexpr uint32_t KERNEL_CODE_END = 0x00010000;
    bool isKernelRAM = physTarget < KERNEL_CODE_END;
    bool isUnaligned = (actualEntry & 3) != 0;
    bool isNotKSEG0 = (actualEntry >> 29) != 4; // 0x80000000 >> 29 = 4

    if (isKernelRAM || isUnaligned || isNotKSEG0) {
      if (IsPs1HleTraceEnabled()) {
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                   "Table 0x%02X[0x%02X] entry corrupted/invalid: 0x%08X — "
                   "using HLE handler (kernel=%d unaligned=%d notKSEG0=%d)",
                   table, func, actualEntry, isKernelRAM, isUnaligned,
                   isNotKSEG0);
      }
    } else {
      if (IsPs1HleTraceEnabled()) {
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                   "Table 0x%02X[0x%02X] patched: expected=0x%08X "
                   "actual=0x%08X → redirecting",
                   table, func, expectedTrampoline, actualEntry);
      }
      cpu.SetPC(actualEntry);
      return false;
    }
  }

  if (IsPs1HleTraceEnabled()) {
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "BIOS call: table=0x%02X func=0x%02X ($ra=0x%08X) "
               "a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X t1=0x%08X",
               table, func, cpu.GetRegister(31), cpu.GetRegister(4),
               cpu.GetRegister(5), cpu.GetRegister(6), cpu.GetRegister(7),
               cpu.GetRegister(9));
  }

  switch (table) {
  case 0xA0:
    DispatchA0(ps1, func);
    return true;
  case 0xB0:
    return DispatchB0(ps1, func);
  case 0xC0:
    DispatchC0(ps1, func);
    return true;
  default:
    return true;
  }
}

// ─── A-Table (0xA0) ─────────────────────────────────────────────────────

void PS1HleBios::DispatchA0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();
  auto &gpu = ps1.GetGPU();
  auto &mem = ps1.GetMemory();

  if (IsPs1HleTraceEnabled()) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.BIOS",
               "A0:0x%02X a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X RA=0x%08X",
               func, cpu.GetRegister(4), cpu.GetRegister(5), cpu.GetRegister(6),
               cpu.GetRegister(7), cpu.GetRegister(31));
  }

  switch (func) {

  // A0:00h open(filename, accessmode) — same as B0:32h
  case 0x00: {
    uint32_t nameAddr = cpu.GetRegister(4);
    uint32_t accessMode = cpu.GetRegister(5);

    auto &log = AIO::Emulator::Common::Logger::Instance();

    if (nameAddr < 0x1000 && (nameAddr & 0x1FFFFFFF) < 0x1000) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "A0:open() spurious: nameAddr=0x%08X → fd=-1", nameAddr);
      cpu.SetRegister(2, 0xFFFFFFFF);
      break;
    }

    char filename[128];
    for (int i = 0; i < 127; i++) {
      filename[i] = static_cast<char>(mem.Read8(nameAddr + i));
      if (filename[i] == '\0')
        break;
    }
    filename[127] = '\0';

    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "A0:open(\"%s\", 0x%X)", filename, accessMode);

    if (std::strstr(filename, "tty") || std::strstr(filename, "TTY")) {
      cpu.SetRegister(2, (accessMode & 0x01) ? 0u : 1u);
    } else {
      cpu.SetRegister(2, 0xFFFFFFFF);
    }
    break;
  }

  // A0:01h lseek(fd, offset, seektype)
  case 0x01:
    cpu.SetRegister(2, 0);
    break;

  // A0:02h read(fd, dst, length)
  case 0x02:
    cpu.SetRegister(2, 0);
    break;

  // A0:03h write(fd, src, length)
  case 0x03: {
    uint32_t len = cpu.GetRegister(6);
    cpu.SetRegister(2, len);
    break;
  }

  // A0:04h close(fd)
  case 0x04:
    cpu.SetRegister(2, cpu.GetRegister(4));
    break;

  // A0:05h ioctl(fd, cmd, arg)
  case 0x05:
    cpu.SetRegister(2, 0);
    break;

  // A0:06h exit(exitcode)
  case 0x06:
    break;

  // A0:07h isatty(fd)
  case 0x07: {
    uint32_t fd = cpu.GetRegister(4);
    cpu.SetRegister(2, (fd <= 1) ? 1u : 0u);
    break;
  }

  // A0:08h getc(fd)
  case 0x08:
    cpu.SetRegister(2, 0xFFFFFFFF);
    break;

  // A0:09h putc(char, fd)
  case 0x09:
    cpu.SetRegister(2, 1);
    break;

  // A0:0Ah todigit(char)
  case 0x0A: {
    uint32_t c = cpu.GetRegister(4) & 0xFF;
    if (c >= '0' && c <= '9')
      cpu.SetRegister(2, c - '0');
    else if (c >= 'A' && c <= 'Z')
      cpu.SetRegister(2, c - 'A' + 10);
    else if (c >= 'a' && c <= 'z')
      cpu.SetRegister(2, c - 'a' + 10);
    else
      cpu.SetRegister(2, 0x0098967F);
    break;
  }

  // A0:0Eh abs(val)
  case 0x0E:
  // A0:0Fh labs(val) — same as abs
  case 0x0F: {
    int32_t val = static_cast<int32_t>(cpu.GetRegister(4));
    cpu.SetRegister(2, static_cast<uint32_t>(val < 0 ? -val : val));
    break;
  }

  // A0:10h atoi(src)
  case 0x10:
  // A0:11h atol(src) — same as atoi
  case 0x11: {
    uint32_t srcAddr = cpu.GetRegister(4);
    int32_t result = 0;
    bool negative = false;
    uint32_t i = 0;
    uint8_t ch = mem.Read8(srcAddr);

    while (ch == ' ' || ch == '\t') {
      i++;
      ch = mem.Read8(srcAddr + i);
    }
    if (ch == '-') {
      negative = true;
      i++;
      ch = mem.Read8(srcAddr + i);
    } else if (ch == '+') {
      i++;
      ch = mem.Read8(srcAddr + i);
    }
    while (ch >= '0' && ch <= '9') {
      result = result * 10 + (ch - '0');
      i++;
      ch = mem.Read8(srcAddr + i);
    }
    cpu.SetRegister(2, static_cast<uint32_t>(negative ? -result : result));
    break;
  }

  // A0:2Ah memcpy(dst, src, len)
  case 0x2A: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    cpu.SetRegister(2, dst);
    // Real BIOS advances argument registers past the copied region
    cpu.SetRegister(4, dst + len);
    cpu.SetRegister(5, src + len);
    cpu.SetRegister(6, 0);
    break;
  }

  // A0:2Bh memset(dst, val, len)
  case 0x2B: {
    uint32_t dst = cpu.GetRegister(4);
    uint8_t val = static_cast<uint8_t>(cpu.GetRegister(5));
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, val);
    }
    cpu.SetRegister(2, dst);
    cpu.SetRegister(4, dst + len);
    cpu.SetRegister(6, 0);
    break;
  }

  // A0:2Ch memmove(dst, src, len)
  case 0x2C: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    cpu.SetRegister(2, dst);
    cpu.SetRegister(4, dst + len);
    cpu.SetRegister(5, src + len);
    cpu.SetRegister(6, 0);
    break;
  }

  // A0:2Dh memcmp(s1, s2, len)
  case 0x2D: {
    uint32_t s1 = cpu.GetRegister(4);
    uint32_t s2 = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    int result = 0;
    for (uint32_t i = 0; i < len; i++) {
      int diff = static_cast<int>(mem.Read8(s1 + i)) -
                 static_cast<int>(mem.Read8(s2 + i));
      if (diff != 0) {
        result = diff;
        break;
      }
    }
    cpu.SetRegister(2, static_cast<uint32_t>(result));
    break;
  }

  // A0:1Bh strlen(s)
  case 0x1B: {
    uint32_t addr = cpu.GetRegister(4);
    uint32_t len = 0;
    while (mem.Read8(addr + len) != 0 && len < 0x100000) {
      len++;
    }
    cpu.SetRegister(2, len);
    break;
  }

  // A0:0x13 setjmp(buf) — save callee-saved registers to buffer
  case 0x13: {
    uint32_t buf = cpu.GetRegister(4);
    mem.Write32(buf + 0x00, cpu.GetRegister(31)); // RA
    mem.Write32(buf + 0x04, cpu.GetRegister(29)); // SP
    mem.Write32(buf + 0x08, cpu.GetRegister(30)); // FP
    for (int i = 0; i < 8; i++) {
      mem.Write32(buf + 0x0C + i * 4, cpu.GetRegister(16 + i)); // r16-r23
    }
    mem.Write32(buf + 0x2C, cpu.GetRegister(28)); // GP
    cpu.SetRegister(2, 0);                        // return 0 on first call
    break;
  }

  // A0:14h longjmp(buf, retval)
  case 0x14: {
    uint32_t buf = cpu.GetRegister(4);
    uint32_t retval = cpu.GetRegister(5);
    cpu.SetRegister(31, mem.Read32(buf + 0x00)); // RA
    cpu.SetRegister(29, mem.Read32(buf + 0x04)); // SP
    cpu.SetRegister(30, mem.Read32(buf + 0x08)); // FP
    for (int i = 0; i < 8; i++) {
      cpu.SetRegister(16 + i, mem.Read32(buf + 0x0C + i * 4));
    }
    cpu.SetRegister(28, mem.Read32(buf + 0x2C)); // GP
    cpu.SetRegister(2, retval ? retval : 1);
    // Jump to the saved RA (setjmp callsite)
    cpu.SetPC(cpu.GetRegister(31));
    break;
  }

  // A0:15h strcat(dst, src)
  case 0x15: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t d = dst;
    while (mem.Read8(d) != 0 && d < dst + 0x100000)
      d++;
    uint32_t s = 0;
    uint8_t ch;
    while ((ch = mem.Read8(src + s)) != 0 && s < 0x100000) {
      mem.Write8(d + s, ch);
      s++;
    }
    mem.Write8(d + s, 0);
    cpu.SetRegister(2, dst);
    break;
  }

  // A0:17h strcmp(s1, s2)
  case 0x17: {
    uint32_t s1 = cpu.GetRegister(4);
    uint32_t s2 = cpu.GetRegister(5);
    int result = 0;
    for (uint32_t i = 0; i < 0x100000; i++) {
      uint8_t c1 = mem.Read8(s1 + i);
      uint8_t c2 = mem.Read8(s2 + i);
      if (c1 != c2) {
        result = static_cast<int>(c1) - static_cast<int>(c2);
        break;
      }
      if (c1 == 0)
        break;
    }
    cpu.SetRegister(2, static_cast<uint32_t>(result));
    break;
  }

  // A0:18h strncmp(s1, s2, n)
  case 0x18: {
    uint32_t s1 = cpu.GetRegister(4);
    uint32_t s2 = cpu.GetRegister(5);
    uint32_t n = cpu.GetRegister(6);
    int result = 0;
    for (uint32_t i = 0; i < n; i++) {
      uint8_t c1 = mem.Read8(s1 + i);
      uint8_t c2 = mem.Read8(s2 + i);
      if (c1 != c2) {
        result = static_cast<int>(c1) - static_cast<int>(c2);
        break;
      }
      if (c1 == 0)
        break;
    }
    cpu.SetRegister(2, static_cast<uint32_t>(result));
    break;
  }

  // A0:19h strcpy(dst, src)
  case 0x19: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t i = 0;
    uint8_t ch;
    while ((ch = mem.Read8(src + i)) != 0 && i < 0x100000) {
      mem.Write8(dst + i, ch);
      i++;
    }
    mem.Write8(dst + i, 0);
    cpu.SetRegister(2, dst);
    {
      char buf[128] = {};
      uint32_t slen = (i < 127) ? i : 127;
      for (uint32_t j = 0; j < slen; j++)
        buf[j] = static_cast<char>(mem.Read8(dst + j));
      auto &log2 = AIO::Emulator::Common::Logger::Instance();
      log2.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                  "strcpy: \"%s\" (len=%u) RA=0x%08X", buf, i,
                  cpu.GetRegister(31));
    }
    break;
  }

  // A0:1Ah strncpy(dst, src, n)
  case 0x1A: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t n = cpu.GetRegister(6);
    uint32_t i = 0;
    bool hitNull = false;
    for (i = 0; i < n; i++) {
      if (!hitNull) {
        uint8_t ch = mem.Read8(src + i);
        mem.Write8(dst + i, ch);
        if (ch == 0)
          hitNull = true;
      } else {
        mem.Write8(dst + i, 0);
      }
    }
    cpu.SetRegister(2, dst);
    break;
  }

  // A0:25h toupper(c)
  case 0x25: {
    uint32_t c = cpu.GetRegister(4) & 0xFF;
    if (c >= 'a' && c <= 'z')
      c -= 32;
    cpu.SetRegister(2, c);
    break;
  }

  // A0:26h tolower(c)
  case 0x26: {
    uint32_t c = cpu.GetRegister(4) & 0xFF;
    if (c >= 'A' && c <= 'Z')
      c += 32;
    cpu.SetRegister(2, c);
    break;
  }

  // A0:28h bzero(ptr, len)
  case 0x28: {
    uint32_t ptr = cpu.GetRegister(4);
    uint32_t len = cpu.GetRegister(5);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(ptr + i, 0);
    }
    break;
  }

  // A0:27h bcopy(src, dst, len) — refuses if src==0, returns src
  case 0x27: {
    uint32_t src = cpu.GetRegister(4);
    uint32_t dst = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    if (src == 0) {
      cpu.SetRegister(2, 0);
      break;
    }
    if (src >= 0xC0000000 || len > 0x200000) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Warning, "PS1.HLE",
                 "bcopy SUSPICIOUS: src=0x%08X dst=0x%08X len=0x%X RA=0x%08X",
                 src, dst, len, cpu.GetRegister(31));
      for (int r = 0; r < 32; r++) {
        log.LogFmt(AIO::Emulator::Common::LogLevel::Warning, "PS1.HLE",
                   "  r%d = 0x%08X", r, cpu.GetRegister(r));
      }
    }
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    cpu.SetRegister(2, src);
    break;
  }

  // A0:2Fh rand()
  case 0x2F: {
    hleSeed = hleSeed * 1103515245 + 12345;
    cpu.SetRegister(2, (hleSeed >> 16) & 0x7FFF);
    break;
  }

  // A0:30h srand(seed)
  case 0x30: {
    hleSeed = cpu.GetRegister(4);
    break;
  }

  // A0:39h InitHeap(base, size)
  case 0x39: {
    heapBase = cpu.GetRegister(4);
    heapSize = cpu.GetRegister(5);
    heapPtr = heapBase;
    break;
  }

  // A0:33h malloc(size) — simple bump allocator
  case 0x33: {
    uint32_t size = cpu.GetRegister(4);
    size = (size + 3) & ~3u;
    if (heapPtr + size <= heapBase + heapSize) {
      cpu.SetRegister(2, heapPtr);
      heapPtr += size;
    } else {
      cpu.SetRegister(2, 0);
    }
    break;
  }

  // A0:34h free(ptr) — no-op for bump allocator
  case 0x34:
    break;

  // A0:3Ah _exit(exitcode)
  case 0x3A:
    break;

  // A0:3Bh getchar() — read from TTY, return 0xFF (no input)
  case 0x3B:
    cpu.SetRegister(2, 0xFFFFFFFF);
    break;

  // A0:3Ch putchar(char) — buffer and flush on newline
  case 0x3C: {
    static std::string ttyLineBuf;
    char ch = static_cast<char>(cpu.GetRegister(4) & 0xFF);
    if (ch == '\n' || ch == '\r') {
      if (!ttyLineBuf.empty()) {
        auto &log = AIO::Emulator::Common::Logger::Instance();
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.TTY",
                   "putchar: %s", ttyLineBuf.c_str());
        ttyLineBuf.clear();
      }
    } else if (ch >= 0x20 && ch < 0x7F) {
      ttyLineBuf += ch;
    }
    cpu.SetRegister(2, cpu.GetRegister(4));
    break;
  }

  // A0:3Dh gets(dst) — no TTY input, return empty string
  case 0x3D: {
    uint32_t dst = cpu.GetRegister(4);
    mem.Write8(dst, 0);
    cpu.SetRegister(2, dst);
    break;
  }

  // A0:3Eh puts(src) — log the string
  case 0x3E: {
    uint32_t srcAddr = cpu.GetRegister(4);
    char buf[256];
    int i = 0;
    for (; i < 255; i++) {
      uint8_t ch = mem.Read8(srcAddr + i);
      if (ch == 0)
        break;
      buf[i] = static_cast<char>(ch);
    }
    buf[i] = '\0';
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.TTY", "puts: \"%s\"",
               buf);
    break;
  }

  // A0:3Fh printf — log the format string (no vararg expansion)
  case 0x3F: {
    uint32_t fmtAddr = cpu.GetRegister(4);
    char buf[256];
    int i = 0;
    for (; i < 255; i++) {
      uint8_t ch = mem.Read8(fmtAddr + i);
      if (ch == 0)
        break;
      buf[i] = static_cast<char>(ch);
    }
    buf[i] = '\0';
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.TTY",
               "printf: \"%s\"", buf);
    break;
  }

  // A0:44h FlushCache — invalidate the entire I-cache
  case 0x44:
    cpu.FlushICache();
    break;

  // A0:46h GPU_dw(Xdst,Ydst,Xsiz,Ysiz,src) — CPU-to-VRAM software transfer
  case 0x46: {
    uint32_t xdst = cpu.GetRegister(4) & 0xFFFF;
    uint32_t ydst = cpu.GetRegister(5) & 0xFFFF;
    uint32_t xsiz = cpu.GetRegister(6) & 0xFFFF;
    uint32_t ysiz = cpu.GetRegister(7) & 0xFFFF;
    uint32_t src = mem.Read32(cpu.GetRegister(29) + 0x10);

    gpu.WriteGP0(0xA0000000);
    gpu.WriteGP0((ydst << 16) | xdst);
    gpu.WriteGP0((ysiz << 16) | xsiz);

    uint32_t nWords = (xsiz * ysiz + 1) / 2;
    for (uint32_t i = 0; i < nWords; i++) {
      gpu.WriteGP0(mem.Read32(src + i * 4));
    }
    cpu.SetRegister(2, src + nWords * 4);
    break;
  }

  // A0:47h gpu_send_dma(Xdst,Ydst,Xsiz,Ysiz,src) — CPU-to-VRAM via DMA (HLE:
  // software path)
  case 0x47: {
    uint32_t xdst = cpu.GetRegister(4) & 0xFFFF;
    uint32_t ydst = cpu.GetRegister(5) & 0xFFFF;
    uint32_t xsiz = cpu.GetRegister(6) & 0xFFFF;
    uint32_t ysiz = cpu.GetRegister(7) & 0xFFFF;
    uint32_t src = mem.Read32(cpu.GetRegister(29) + 0x10);

    gpu.WriteGP0(0xA0000000);
    gpu.WriteGP0((ydst << 16) | xdst);
    gpu.WriteGP0((ysiz << 16) | xsiz);

    uint32_t nWords = (xsiz * ysiz + 1) / 2;
    for (uint32_t i = 0; i < nWords; i++) {
      gpu.WriteGP0(mem.Read32(src + i * 4));
    }
    cpu.SetRegister(2, 0x1F801810);
    break;
  }

  // A0:48h SendGP1Command(gp1cmd)
  case 0x48: {
    gpu.WriteGP1(cpu.GetRegister(4));
    break;
  }

  // A0:49h GPU_cw(cmd) — send GP0 command word
  case 0x49: {
    gpu.WriteGP0(cpu.GetRegister(4));
    break;
  }

  // A0:4Ah GPU_cwp(addr, count) — send GP0 command words from buffer
  case 0x4A: {
    uint32_t addr = cpu.GetRegister(4);
    uint32_t count = cpu.GetRegister(5);
    if (count > 0x40000)
      count = 0x40000;
    for (uint32_t i = 0; i < count; i++) {
      gpu.WriteGP0(mem.Read32(addr + i * 4));
    }
    break;
  }

  // A0:4Bh send_gpu_linked_list(addr) — OT rendering
  case 0x4B: {
    uint32_t addr = cpu.GetRegister(4) & 0x1FFFFC;
    for (int safety = 0; safety < 0x100000; safety++) {
      uint32_t header = mem.Read32(0x80000000 | addr);
      uint32_t wordCount = header >> 24;
      for (uint32_t i = 0; i < wordCount; i++) {
        gpu.WriteGP0(mem.Read32(0x80000000 | ((addr + 4 + i * 4) & 0x1FFFFC)));
      }
      if ((header & 0xFFFFFF) == 0xFFFFFF)
        break;
      addr = header & 0x1FFFFC;
    }
    break;
  }

  // A0:4Ch gpu_abort_dma() — stop GPU DMA and reset display state
  case 0x4C: {
    gpu.WriteGP1(0x04000000);
    gpu.WriteGP1(0x02000000);
    gpu.WriteGP1(0x01000000);
    cpu.SetRegister(2, 0x1F801814);
    break;
  }

  // A0:4Dh GetGPUStatus() — read GPUSTAT register
  case 0x4D: {
    cpu.SetRegister(2, gpu.ReadGPUSTAT());
    break;
  }

  // A0:4Eh gpu_sync() — HLE: GPU is always ready
  case 0x4E: {
    cpu.SetRegister(2, 0);
    break;
  }

  // A0:70h _bu_init — no-op
  case 0x70:
    break;

  // A0:54h / A0:71h _96_init — CDROM subsystem initialization
  // The real BIOS enqueues CDROM IRQ handlers, opens CDROM events,
  // and resets the CDROM hardware. In HLE mode, our CDROM is initialized
  // at boot and IRQs are handled directly in hardware emulation.
  // Re-initialize the CDROM hardware registers to ensure a clean state.
  case 0x54:
  case 0x71: {
    auto &cdrom = ps1.GetCDROM();
    InitCDROM(cdrom);
    cdromHandlersRegistered = true;
    break;
  }

  // A0:72h deinitCDRom — deregisters CDROM handler chain entries.
  // Games that handle CDROM in their own exception handler call this
  // to disable the BIOS CDROM state machine before taking over.
  case 0x72:
    cdromHandlersRegistered = false;
    break;

  // A0:90h-93h CdromIoIrqFunc / CdromDmaIrqFunc — internal IRQ handlers
  // In HLE mode, CDROM IRQs are handled directly by hardware emulation
  case 0x90:
  case 0x91:
  case 0x92:
  case 0x93:
    break;

  // A0:95h CdInitSubFunc — subfunction for _96_init
  // Hardware CDROM init is done by InitCDROM; this is a no-op in HLE
  case 0x95:
    cpu.SetRegister(2, 1);
    break;

  // A0:96h AddCDROMDevice — already set up in InitKernelState DCBs
  case 0x96:
    break;

  // A0:97h AddMemCardDevice — already set up in InitKernelState DCBs
  case 0x97:
    break;

  // A0:9Eh SetCdromIrqAutoAbort(type, flag) — no-op in HLE
  case 0x9E:
    break;

  // A0:9Fh SetMem(megabytes) — restrict effective RAM window via 0x1F801060
  case 0x9F: {
    uint32_t megabytes = cpu.GetRegister(4);
    uint32_t regVal = (megabytes == 2) ? 0x00000888u : 0x00000B88u;
    mem.Write32(0x1F801060, regVal);
    mem.WriteRAM32(0x60, megabytes);
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.BIOS",
               "SetMem(%u) → RAM_SIZE=0x%08X", megabytes, regVal);
    break;
  }

  // A0:A2h EnqueueCdIntr — IRQ chain managed by hardware emulation
  case 0xA2:
    break;

  // A0:A3h DequeueCdIntr — broken in real BIOS, no-op
  case 0xA3:
    break;

  // A0:16h strncat(dst, src, n)
  case 0x16: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t n = cpu.GetRegister(6);
    uint32_t d = dst;
    while (mem.Read8(d) != 0 && d < dst + 0x100000) d++;
    uint32_t i = 0;
    uint8_t ch;
    while (i < n && (ch = mem.Read8(src + i)) != 0 && i < 0x100000) {
      mem.Write8(d + i, ch); i++;
    }
    mem.Write8(d + i, 0);
    cpu.SetRegister(2, dst);
    break;
  }

  // A0:1Ch index(s, c) = strchr — first occurrence
  case 0x1C:
  // A0:1Eh strchr(s, c) — duplicate entry
  case 0x1E: {
    uint32_t s = cpu.GetRegister(4);
    uint8_t c = static_cast<uint8_t>(cpu.GetRegister(5));
    bool found = false;
    for (uint32_t i = 0; i < 0x100000; i++) {
      uint8_t ch = mem.Read8(s + i);
      if (ch == c) { 
        cpu.SetRegister(2, s + i);
        found = true;
        break;
      }
      if (ch == 0) break;
    }
    if (!found) cpu.SetRegister(2, 0);
    break;
  }

  // A0:1Dh rindex(s, c) = strrchr — last occurrence
  case 0x1D:
  // A0:1Fh strrchr(s, c) — duplicate entry
  case 0x1F: {
    uint32_t s = cpu.GetRegister(4);
    uint8_t c = static_cast<uint8_t>(cpu.GetRegister(5));
    uint32_t last = 0;
    bool found = false;
    for (uint32_t i = 0; i < 0x100000; i++) {
      uint8_t ch = mem.Read8(s + i);
      if (ch == c) { last = s + i; found = true; }
      if (ch == 0) break;
    }
    cpu.SetRegister(2, found ? last : 0u);
    break;
  }

  // A0:20h strpbrk(s, accept) — first char in s that is in accept
  case 0x20: {
    uint32_t s = cpu.GetRegister(4);
    uint32_t accept = cpu.GetRegister(5);
    bool found = false;
    for (uint32_t i = 0; i < 0x100000; i++) {
      uint8_t ch = mem.Read8(s + i);
      if (ch == 0) { cpu.SetRegister(2, 0); found = true; break; }
      for (uint32_t j = 0; j < 0x100000; j++) {
        uint8_t ac = mem.Read8(accept + j);
        if (ac == 0) break;
        if (ch == ac) { cpu.SetRegister(2, s + i); found = true; break; }
      }
      if (found) break;
    }
    if (!found) cpu.SetRegister(2, 0);
    break;
  }

  // A0:21h strspn(s, accept) — length of prefix of s made of accept-chars
  case 0x21: {
    uint32_t s = cpu.GetRegister(4);
    uint32_t accept = cpu.GetRegister(5);
    uint32_t len = 0;
    for (; len < 0x100000; len++) {
      uint8_t ch = mem.Read8(s + len);
      if (ch == 0) break;
      bool inAccept = false;
      for (uint32_t j = 0; j < 0x100000; j++) {
        uint8_t ac = mem.Read8(accept + j);
        if (ac == 0) break;
        if (ch == ac) { inAccept = true; break; }
      }
      if (!inAccept) break;
    }
    cpu.SetRegister(2, len);
    break;
  }

  // A0:22h strcspn(s, reject) — length of prefix of s with no reject-chars
  case 0x22: {
    uint32_t s = cpu.GetRegister(4);
    uint32_t reject = cpu.GetRegister(5);
    uint32_t len = 0;
    for (; len < 0x100000; len++) {
      uint8_t ch = mem.Read8(s + len);
      if (ch == 0) break;
      bool inReject = false;
      for (uint32_t j = 0; j < 0x100000; j++) {
        uint8_t rj = mem.Read8(reject + j);
        if (rj == 0) break;
        if (ch == rj) { inReject = true; break; }
      }
      if (inReject) break;
    }
    cpu.SetRegister(2, len);
    break;
  }

  // A0:23h strtok(s, delim) — tokenize string; static next-pointer state
  case 0x23: {
    static uint32_t strtokNext = 0;
    uint32_t s = cpu.GetRegister(4);
    uint32_t delim = cpu.GetRegister(5);
    if (s != 0) strtokNext = s;
    if (strtokNext == 0) { cpu.SetRegister(2, 0); break; }
    // Skip leading delimiters
    bool skipping = true;
    while (skipping && strtokNext < 0x001FFFFF) {
      uint8_t ch = mem.Read8(strtokNext);
      if (ch == 0) { strtokNext = 0; cpu.SetRegister(2, 0); break; }
      bool isDelim = false;
      for (uint32_t j = 0; j < 256; j++) {
        uint8_t dc = mem.Read8(delim + j);
        if (dc == 0) break;
        if (ch == dc) { isDelim = true; break; }
      }
      if (!isDelim) { skipping = false; } else { strtokNext++; }
    }
    if (!skipping && strtokNext < 0x001FFFFF) {
      uint32_t tokenStart = strtokNext;
      bool finding = true;
      while (finding && strtokNext < 0x001FFFFF) {
        uint8_t ch = mem.Read8(strtokNext);
        if (ch == 0) { strtokNext = 0; finding = false; break; }
        bool isDelim = false;
        for (uint32_t j = 0; j < 256; j++) {
          uint8_t dc = mem.Read8(delim + j);
          if (dc == 0) break;
          if (ch == dc) { isDelim = true; break; }
        }
        if (isDelim) { mem.Write8(strtokNext, 0); strtokNext++; finding = false; break; }
        strtokNext++;
      }
      cpu.SetRegister(2, tokenStart);
    }
    break;
  }

  // A0:24h strstr(s, sub) — first occurrence of sub in s
  case 0x24: {
    uint32_t s = cpu.GetRegister(4);
    uint32_t sub = cpu.GetRegister(5);
    // Empty sub matches s
    if (mem.Read8(sub) == 0) { cpu.SetRegister(2, s); break; }
    uint32_t result = 0;
    for (uint32_t i = 0; i < 0x100000; i++) {
      if (mem.Read8(s + i) == 0) break;
      bool match = true;
      for (uint32_t j = 0; j < 0x100000; j++) {
        uint8_t sc = mem.Read8(sub + j);
        if (sc == 0) break;
        if (mem.Read8(s + i + j) != sc) { match = false; break; }
      }
      if (match) { result = s + i; break; }
    }
    cpu.SetRegister(2, result);
    break;
  }

  // A0:29h memchr(s, c, n) — first occurrence of c in s[0..n-1]
  case 0x29: {
    uint32_t s = cpu.GetRegister(4);
    uint8_t c = static_cast<uint8_t>(cpu.GetRegister(5));
    uint32_t n = cpu.GetRegister(6);
    uint32_t result = 0;
    for (uint32_t i = 0; i < n && i < 0x100000; i++) {
      if (mem.Read8(s + i) == c) { result = s + i; break; }
    }
    cpu.SetRegister(2, result);
    break;
  }

  // A0:31h qsort(base, n, size, compare) — safe no-op (compare fn is PS1 code)
  case 0x31:
    // Cannot call PS1 code from C++ dispatch without re-entering the CPU.
    // Return without error — games that need sorted data will see unsorted;
    // this is better than a crash from mishandled function pointer execution.
    break;

  default:
    cpu.SetRegister(2, 0);
    break;
  }
}

// ─── B-Table (0xB0) ─────────────────────────────────────────────────────
// Per PSX-SPX: https://psx-spx.consoledev.net/kernelbios/#bios-function-summary
// GPU functions do NOT exist in the B-table; they are A-table only (A0:47-4B).

bool PS1HleBios::DispatchB0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();
  auto &mem = ps1.GetMemory();

  if (IsPs1HleTraceEnabled()) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.BIOS",
               "B0:0x%02X a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X RA=0x%08X",
               func, cpu.GetRegister(4), cpu.GetRegister(5), cpu.GetRegister(6),
               cpu.GetRegister(7), cpu.GetRegister(31));
  }

  switch (func) {

  // B0:00h alloc_kernel_memory(size)
  case 0x00: {
    uint32_t size = cpu.GetRegister(4);
    size = (size + 3) & ~3u; // align to 4 bytes
    if (kernelHeapPtr + size <= kernelHeapBase + kernelHeapSize) {
      uint32_t addr = kernelHeapPtr;
      kernelHeapPtr += size;
      cpu.SetRegister(2, 0x80000000 | addr);
    } else {
      cpu.SetRegister(2, 0);
    }
    break;
  }

  // B0:01h free_kernel_memory(buf)
  case 0x01:
    break;

  // B0:02h init_timer(t, target, flags)
  // Real BIOS writes timer HW registers: base mode 0x0048 (resetOnTarget +
  // irqRepeat) flags & 0x0010 → sync enable, flags & 0x0001 → external clock,
  // flags & 0x1000 → irqOnTarget
  case 0x02: {
    uint32_t t = cpu.GetRegister(4) & 0xFFFF;
    uint16_t target = static_cast<uint16_t>(cpu.GetRegister(5));
    uint16_t flags = static_cast<uint16_t>(cpu.GetRegister(6));
    if (t < 3) {
      auto &timers = ps1.GetTimers();
      uint32_t timerBase = IO::TIMER_BASE + t * IO::TIMER_CHANNEL_SIZE;
      timers.Write32(timerBase + 0x04, 0); // reset mode first (clears counter)
      timers.Write32(timerBase + 0x08, target); // set target
      uint16_t mode = 0x0048; // resetOnTarget(3) + irqRepeat(6)
      if (flags & 0x0010)
        mode |= 0x0001; // sync enable
      if (flags & 0x0001)
        mode |= 0x0100; // external clock source
      if (flags & 0x1000)
        mode |= 0x0010; // irqOnTarget
      timers.Write32(timerBase + 0x04,
                     mode); // write final mode (resets counter)
    }
    cpu.SetRegister(2, (t < 3) ? 1u : 0u);
    break;
  }

  // B0:03h get_timer(t) — read current counter value
  case 0x03: {
    uint32_t t = cpu.GetRegister(4) & 0xFFFF;
    uint32_t val = 0;
    if (t < 3) {
      auto &timers = ps1.GetTimers();
      val = timers.Read32(IO::TIMER_BASE + t * IO::TIMER_CHANNEL_SIZE);
    }
    cpu.SetRegister(2, val);
    break;
  }

  // B0:04h enable_timer_irq(t) — set I_MASK bit for timer
  case 0x04: {
    uint32_t t = cpu.GetRegister(4) & 0xFFFF;
    static constexpr uint32_t timerMasks[] = {IRQ::TIMER0, IRQ::TIMER1,
                                              IRQ::TIMER2, IRQ::VBLANK};
    if (t <= 3) {
      auto &irqs = ps1.GetInterrupts();
      irqs.WriteMask(irqs.ReadMask() | timerMasks[t]);
    }
    cpu.SetRegister(2, (t <= 2) ? 1u : 0u);
    break;
  }

  // B0:05h disable_timer_irq(t) — clear I_MASK bit for timer
  case 0x05: {
    uint32_t t = cpu.GetRegister(4) & 0xFFFF;
    static constexpr uint32_t timerMasks[] = {IRQ::TIMER0, IRQ::TIMER1,
                                              IRQ::TIMER2, IRQ::VBLANK};
    if (t <= 3) {
      auto &irqs = ps1.GetInterrupts();
      irqs.WriteMask(irqs.ReadMask() & ~timerMasks[t]);
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:06h restart_timer(t) — reset counter to 0
  case 0x06: {
    uint32_t t = cpu.GetRegister(4) & 0xFFFF;
    if (t < 3) {
      auto &timers = ps1.GetTimers();
      timers.Write32(IO::TIMER_BASE + t * IO::TIMER_CHANNEL_SIZE, 0);
    }
    cpu.SetRegister(2, (t < 3) ? 1u : 0u);
    break;
  }

  // B0:07h DeliverEvent(class, spec)
  case 0x07: {
    uint32_t classId = cpu.GetRegister(4);
    uint32_t spec = cpu.GetRegister(5);
    DeliverEvent(classId, spec);
    break;
  }

  // B0:08h OpenEvent(class, spec, mode, func)
  case 0x08: {
    uint32_t classId = cpu.GetRegister(4);
    uint32_t spec = cpu.GetRegister(5);
    uint32_t mode = cpu.GetRegister(6);
    uint32_t funcAddr = cpu.GetRegister(7);

    int slot = -1;
    for (int i = 0; i < MAX_EVENTS; i++) {
      if (!events[i].used) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      events[slot] = {classId, spec, mode, funcAddr, true, false, false};
      events[slot].enabled = false;
      WriteEvCBToRAM(slot);
    }
    {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "OpenEvent: class=0x%08X spec=0x%04X mode=0x%04X "
                 "func=0x%08X → slot=%d",
                 classId, spec, mode, funcAddr, slot);
    }
    cpu.SetRegister(2, (slot >= 0) ? (0xF1000000u | static_cast<uint32_t>(slot))
                                   : 0xFFFFFFFF);
    break;
  }

  // B0:09h CloseEvent(event)
  case 0x09: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot] = {};
      WriteEvCBToRAM(slot);
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:0Ah WaitEvent(event)
  case 0x0A: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      ReadEvCBFromRAM(slot);
    }
    bool ready = (slot < MAX_EVENTS && events[slot].fired);
    if (IsPs1HleTraceEnabled() && slot < MAX_EVENTS) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      const auto &ev = events[slot];
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "WaitEvent: slot=%d class=0x%08X spec=0x%04X mode=0x%04X "
                 "enabled=%d fired=%d -> %u",
                 slot, ev.classId, ev.spec, ev.mode, ev.enabled ? 1 : 0,
                 ev.fired ? 1 : 0, ready ? 1u : 0u);
    }
    if (ready) {
      events[slot].fired = false;
      WriteEvCBToRAM(slot);
      cpu.SetRegister(2, 1);
    } else {
      cpu.SetRegister(2, 0);
    }
    break;
  }

  // B0:0Bh TestEvent(event)
  case 0x0B: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      ReadEvCBFromRAM(slot);
    }
    bool ready = (slot < MAX_EVENTS && events[slot].fired);
    if (IsPs1HleTraceEnabled() && slot < MAX_EVENTS) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      const auto &ev = events[slot];
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "TestEvent: slot=%d class=0x%08X spec=0x%04X mode=0x%04X "
                 "enabled=%d fired=%d -> %u",
                 slot, ev.classId, ev.spec, ev.mode, ev.enabled ? 1 : 0,
                 ev.fired ? 1 : 0, ready ? 1u : 0u);
    }
    if (ready) {
      events[slot].fired = false;
      WriteEvCBToRAM(slot);
    }
    cpu.SetRegister(2, ready ? 1u : 0u);
    break;
  }

  // B0:0Ch EnableEvent(event)
  case 0x0C: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = true;
      events[slot].fired = false;
      WriteEvCBToRAM(slot);

      if (IsPs1HleTraceEnabled()) {
        auto &log = AIO::Emulator::Common::Logger::Instance();
        const auto &ev = events[slot];
        log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                   "EnableEvent: slot=%d class=0x%08X spec=0x%04X mode=0x%04X",
                   slot, ev.classId, ev.spec, ev.mode);
      }

      uint32_t cls = events[slot].classId;
      if ((cls & 0xFF000000) == 0xF2000000) {
        uint32_t rcIndex = cls & 0xF;
        auto &irqs = ps1.GetInterrupts();
        static constexpr uint32_t rcMasks[] = {IRQ::TIMER0, IRQ::TIMER1,
                                               IRQ::TIMER2, IRQ::VBLANK};
        if (rcIndex <= 3) {
          irqs.WriteMask(irqs.ReadMask() | rcMasks[rcIndex]);
        }
      }
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:0Dh DisableEvent(event)
  case 0x0D: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = false;
      WriteEvCBToRAM(slot);
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:12h InitPAD2(buf1,siz1,buf2,siz2)
  case 0x12:
    padBuf1Addr = cpu.GetRegister(4);
    padBuf1Size = cpu.GetRegister(5);
    padBuf2Addr = cpu.GetRegister(6);
    padBuf2Size = cpu.GetRegister(7);
    padStarted = false;
    cpu.SetRegister(2, 1);
    break;

  // B0:13h StartPAD2
  case 0x13:
    padStarted = true;
    break;

  // B0:14h StopPAD2
  case 0x14:
    padStarted = false;
    break;

  // B0:15h PAD_init2(type, button_dest, unused, unused)
  case 0x15:
    padBuf1Addr = cpu.GetRegister(5);
    padBuf1Size = 4;
    padStarted = true;
    cpu.SetRegister(2, 2);
    break;

  // B0:16h PAD_dr — return current button state from controller
  case 0x16: {
    auto &ctrl = ps1.GetController();
    uint16_t buttons = ctrl.GetButtonState();
    cpu.SetRegister(2, static_cast<uint32_t>(buttons));
    break;
  }

  // B0:17h ReturnFromException — restore CPU state from exception entry.
  // Game DMA and decompressor can overwrite the TCB in RAM, so we use the
  // saved copies captured at HandleException entry (same approach as
  // DispatchNextCallbackOrResume's fast-return path).
  case 0x17: {
    inExceptionHandler = false;

    uint32_t epc = savedEpc;
    uint32_t sr = savedSR;

    for (int i = 1; i < 32; i++) {
      if (i == 26)
        continue;
      cpu.SetRegister(i, savedRegs[i]);
    }

    cpu.SetHI(savedHI);
    cpu.SetLO(savedLO);

    if (IsPs1HleTraceEnabled()) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "ReturnFromException: epc=0x%08X ra=0x%08X sr=0x%08X "
                 "sp=0x%08X (from saved state)",
                 epc, savedRegs[31], sr, savedRegs[29]);
    }

    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);

    cpu.SetPC(epc);
    return false;
  }
  case 0x18:
    hookedEntryIntHandler = 0;
    cpu.SetRegister(2, 0);
    break;

  // B0:19h HookEntryInt(addr)
  case 0x19: {
    hookedEntryIntHandler = cpu.GetRegister(4);
    auto &log2 = AIO::Emulator::Common::Logger::Instance();
    char buf[128];
    snprintf(buf, sizeof(buf), "HookEntryInt: handler=0x%08X",
             hookedEntryIntHandler);
    log2.Log(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE", buf);
    break;
  }

  // B0:20h UnDeliverEvent(class, spec)
  case 0x20: {
    uint32_t classId = cpu.GetRegister(4);
    uint32_t spec = cpu.GetRegister(5);
    for (int i = 0; i < MAX_EVENTS; i++) {
      auto &ev = events[i];
      if (ev.used && ev.enabled && ev.fired && ev.classId == classId &&
          ev.spec == spec && ev.mode == 0x2000) {
        ev.fired = false;
        WriteEvCBToRAM(i);
      }
    }
    break;
  }

  // B0:32h open(filename, accessmode) — file I/O
  case 0x32: {
    uint32_t nameAddr = cpu.GetRegister(4);
    uint32_t accessMode = cpu.GetRegister(5);

    auto &log = AIO::Emulator::Common::Logger::Instance();

    // Guard: if nameAddr is clearly invalid (below kernel structures area),
    // this is likely a side-effect call from patchA0table() or similar.
    // The game isn't trying to actually open a file — just return stdin fd.
    if (nameAddr < 0x1000 && (nameAddr & 0x1FFFFFFF) < 0x1000) {
      log.LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
          "open() spurious call: nameAddr=0x%08X mode=0x%X ra=0x%08X → fd=-1",
          nameAddr, accessMode, cpu.GetRegister(31));
      cpu.SetRegister(2, 0xFFFFFFFF);
      break;
    }

    // Read the filename string
    uint32_t physName = nameAddr & 0x1FFFFFFF;
    char filename[128];
    for (int i = 0; i < 127; i++) {
      filename[i] = static_cast<char>(mem.Read8(nameAddr + i));
      if (filename[i] == '\0')
        break;
    }
    filename[127] = '\0';

    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "open(\"%s\", 0x%X) nameAddr=0x%08X ra=0x%08X", filename,
               accessMode, nameAddr, cpu.GetRegister(31));

    if (std::strstr(filename, "tty") || std::strstr(filename, "TTY")) {
      // TTY device — return fd 0 for read, fd 1 for write
      cpu.SetRegister(2, (accessMode & 0x01) ? 0u : 1u);
    } else if (std::strstr(filename, "bu") || std::strstr(filename, "BU")) {
      cpu.SetRegister(2, 0xFFFFFFFF);
    } else {
      cpu.SetRegister(2, 0xFFFFFFFF);
    }
    break;
  }

  // B0:33h lseek(fd, offset, seektype)
  case 0x33:
    cpu.SetRegister(2, 0);
    break;

  // B0:34h read(fd, dst, length)
  case 0x34:
    cpu.SetRegister(2, 0);
    break;

  // B0:35h write(fd, src, length)
  case 0x35: {
    // For tty writes, just consume the data
    uint32_t len = cpu.GetRegister(6);
    cpu.SetRegister(2, len);
    break;
  }

  // B0:36h close(fd)
  case 0x36:
    cpu.SetRegister(2, cpu.GetRegister(4)); // return fd on success
    break;

  // B0:37h ioctl(fd, cmd, arg)
  case 0x37:
    cpu.SetRegister(2, 0);
    break;

  // B0:38h exit(exitcode)
  case 0x38:
    break;

  // B0:39h isatty(fd)
  case 0x39: {
    uint32_t fd = cpu.GetRegister(4);
    cpu.SetRegister(2, (fd <= 1) ? 1u : 0u);
    break;
  }

  // B0:3Ah getc(fd)
  case 0x3A:
    cpu.SetRegister(2, 0xFFFFFFFF); // EOF
    break;

  // B0:3Bh putc(char, fd)
  case 0x3B:
    cpu.SetRegister(2, 1);
    break;

  // B0:3Ch getchar
  case 0x3C:
    cpu.SetRegister(2, 0xFFFFFFFF);
    break;

  // B0:3Dh putchar(char)
  case 0x3D:
    break;

  // B0:3Eh gets(dst)
  case 0x3E:
    mem.Write8(cpu.GetRegister(4), 0);
    cpu.SetRegister(2, cpu.GetRegister(4));
    break;

  // B0:3Fh puts(src) — log the string
  case 0x3F: {
    uint32_t srcAddr = cpu.GetRegister(4);
    auto &mem2 = ps1.GetMemory();
    char buf[256];
    int i = 0;
    for (; i < 255; i++) {
      uint8_t ch = mem2.Read8(srcAddr + i);
      if (ch == 0)
        break;
      buf[i] = static_cast<char>(ch);
    }
    buf[i] = '\0';
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.TTY", "puts: \"%s\"",
               buf);
    break;
  }

  // B0:40h cd(name) — change directory
  case 0x40:
    cpu.SetRegister(2, 1);
    break;

  // B0:47h AddDrv(device_info)
  case 0x47:
    cpu.SetRegister(2, 1);
    break;

  // B0:48h DelDrv(device_name)
  case 0x48:
    cpu.SetRegister(2, 1);
    break;

  // B0:49h PrintInstalledDevices
  case 0x49:
    break;

  // B0:4Ah InitCARD2(pad_enable)
  case 0x4A:
  // B0:4Bh StartCARD2
  case 0x4B:
  // B0:4Ch StopCARD2
  case 0x4C:
  // B0:4Dh _card_info_subfunc(port)
  case 0x4D:
    cpu.SetRegister(2, 1);
    break;

  // B0:54h _get_errno
  case 0x54:
    cpu.SetRegister(2, 0);
    break;

  // B0:55h _get_error(fd)
  case 0x55:
    cpu.SetRegister(2, 0);
    break;

  // B0:56h GetC0Table — return pointer to C0 function jump table in RAM
  case 0x56:
    cpu.SetRegister(2, 0x80000000 | c0TableRamAddr);
    break;

  // B0:57h GetB0Table — return pointer to B0 function jump table in RAM
  case 0x57:
    cpu.SetRegister(2, 0x80000000 | b0TableRamAddr);
    break;

  // B0:5Bh ChangeClearPAD(int) — enable VBlank IRQ for pad/card
  case 0x5B: {
    auto &irqs = ps1.GetInterrupts();
    uint32_t mask = irqs.ReadMask();
    mask |= IRQ::VBLANK;
    irqs.WriteMask(mask);

    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr |= CPU::SR::IEc | (1u << 10);
    cpu.SetCOP0(CPU::COP0::SR, sr);

    cpu.SetRegister(2, 0);
    break;
  }

  default:
    cpu.SetRegister(2, 0);
    break;
  }

  return true;
}

// ─── C-Table (0xC0) ─────────────────────────────────────────────────────

void PS1HleBios::DispatchC0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();

  if (IsPs1HleTraceEnabled()) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.BIOS",
               "C0:0x%02X a0=0x%08X a1=0x%08X RA=0x%08X", func,
               cpu.GetRegister(4), cpu.GetRegister(5), cpu.GetRegister(31));
  }

  switch (func) {

  // C0:00h EnqueueTimerAndVblankIrqs
  // Real BIOS: clears IMASK timer/VBlank bits, resets all 3 timers,
  // installs timer/VBlank handlers in the priority chain, then re-enables
  // those IRQ bits. HLE skips the handler-chain plumbing and re-enables
  // the IRQ bits directly so VBlank and timer interrupts remain functional.
  case 0x00: {
    auto &irqs = ps1.GetInterrupts();
    irqs.WriteMask(irqs.ReadMask() &
                   ~(IRQ::VBLANK | IRQ::TIMER0 | IRQ::TIMER1 | IRQ::TIMER2));
    auto &timers = ps1.GetTimers();
    for (uint32_t i = 0; i < 3; i++) {
      uint32_t base = IO::TIMER_BASE + i * IO::TIMER_CHANNEL_SIZE;
      timers.Write32(base + 0x04, 0); // mode = 0 (also resets counter)
      timers.Write32(base + 0x08, 0); // target = 0
      timers.Write32(base + 0x00, 0); // counter = 0
    }
    changeClearRCntFlags = {1, 1, 1, 1};
    // Re-enable timer/VBlank IRQs: on real hardware the installed handlers
    // re-enable these bits when they execute; in HLE we do it here directly.
    irqs.WriteMask(irqs.ReadMask() | IRQ::VBLANK | IRQ::TIMER0 | IRQ::TIMER1 |
                   IRQ::TIMER2);
    break;
  }

  // C0:01h EnqueueSyscallHandler — no-op
  case 0x01:
    break;

  // C0:02h SysEnqIntRP(priority, entryPtr)
  // Inserts a handler entry at the head of the chain for the given priority.
  // Handler entry struct in RAM: [next_ptr, handler_func, ...]
  case 0x02: {
    auto &mem = ps1.GetMemory();
    uint32_t priority = cpu.GetRegister(4);
    uint32_t entryPtr = cpu.GetRegister(5);
    if (priority < NUM_HANDLER_PRIORITIES && handlersArrayAddr != 0) {
      uint32_t slotAddr = handlersArrayAddr + priority * 8;
      uint32_t oldHead = mem.Read32(0x80000000 | slotAddr);
      // entry->next = oldHead
      mem.Write32(entryPtr + 0, oldHead);
      // handlersArray[priority].first = entryPtr
      mem.Write32(0x80000000 | slotAddr, entryPtr);
    }
    {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      uint32_t func2 = mem.Read32(entryPtr + 0x04);
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "SysEnqIntRP: priority=%u entry=0x%08X func2=0x%08X", priority,
                 entryPtr, func2);
    }
    cpu.SetRegister(2, 0);
    break;
  }

  // C0:03h SysDeqIntRP(priority, entryPtr)
  // Removes a handler entry from the chain for the given priority.
  case 0x03: {
    auto &mem = ps1.GetMemory();
    uint32_t priority = cpu.GetRegister(4);
    uint32_t entryPtr = cpu.GetRegister(5);
    if (priority < NUM_HANDLER_PRIORITIES && handlersArrayAddr != 0) {
      uint32_t slotAddr = handlersArrayAddr + priority * 8;
      uint32_t prevAddr = 0x80000000 | slotAddr;
      uint32_t current = mem.Read32(prevAddr);
      while (current != 0 && current != entryPtr) {
        prevAddr = current; // next pointer is at offset 0
        current = mem.Read32(current);
      }
      if (current == entryPtr) {
        uint32_t next = mem.Read32(entryPtr + 0);
        mem.Write32(prevAddr, next);
      }
    }
    cpu.SetRegister(2, 0);
    break;
  }

  // C0:07h InstallExceptionHandlers — no-op (HLE handles exceptions)
  case 0x07:
    break;

  // C0:08h SysInitMemory(addr, size) — initialize kernel heap
  case 0x08: {
    uint32_t addr = cpu.GetRegister(4) & 0x1FFFFF;
    uint32_t size = cpu.GetRegister(5);
    kernelHeapBase = addr;
    kernelHeapSize = size;
    kernelHeapPtr = addr;
    break;
  }

  // C0:09h SysInitKMem — no-op
  case 0x09:
    break;

  // C0:0Ah ChangeClearRCnt(t, flag) — controls auto-ack behavior for timer IRQs
  case 0x0A: {
    uint32_t t = cpu.GetRegister(4);
    uint32_t flag = cpu.GetRegister(5);

    uint32_t oldFlag = 0;
    if (t < 4) {
      oldFlag = changeClearRCntFlags[t];
      changeClearRCntFlags[t] = flag;
      if (memoryPtr)
        memoryPtr->WriteRAM32(0x8600 + t * 4, flag);
    }
    cpu.SetRegister(2, oldFlag);
    break;
  }

  // C0:0Ch EnterCriticalSection — disable IRQs, return old state
  case 0x0C: {
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    cpu.SetRegister(2, sr & CPU::SR::IEc);
    cpu.SetCOP0(CPU::COP0::SR, sr & ~CPU::SR::IEc);
    interruptsEnabled = false;
    break;
  }

  // C0:0Dh ExitCriticalSection — re-enable IRQs
  case 0x0D: {
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    cpu.SetCOP0(CPU::COP0::SR, sr | CPU::SR::IEc);
    interruptsEnabled = true;
    break;
  }

  // C0:12h InstallDevices(tty_flag) — initialize file and device tables.
  // PSY-Q runtime calls this via the C0 table pointer (not through 0xC0
  // vector) to set up the kernel's file I/O system. Our InitKernelState
  // already pre-initialized FCBs and DCBs, so this is a no-op.
  case 0x12: {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "C0:12h InstallDevices($a0=%u) called at ra=0x%08X",
               cpu.GetRegister(4), cpu.GetRegister(31));
    break;
  }

  // C0:13h FlushStdInOutPut / reopenStdio — close and reopen stdio as TTY.
  // Called by PSY-Q to ensure fd 0 and 1 are open on the TTY device.
  // Our InitKernelState already pre-opened them, so this is a no-op.
  case 0x13: {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "C0:13h FlushStdInOutPut called at ra=0x%08X",
               cpu.GetRegister(31));
    break;
  }

  // C0:15h _cdevinput — console device input, return no char available
  case 0x15:
    cpu.SetRegister(2, 0);
    break;

  // C0:16h _cdevscan — console device scan, return no char available
  case 0x16:
    cpu.SetRegister(2, 0);
    break;

  // C0:17h _circgetc — circular buffer getc, return EOF
  case 0x17:
    cpu.SetRegister(2, 0xFFFFFFFF);
    break;

  // C0:1Ch SetConf — no-op
  case 0x1C:
    break;

  default:
    cpu.SetRegister(2, 0);
    break;
  }
}

// ─── EvCB RAM Sync ──────────────────────────────────────────────────────

void PS1HleBios::WriteEvCBToRAM(int slot) {
  if (!memoryPtr || slot < 0 || slot >= MAX_EVENTS)
    return;
  uint32_t addr = EVCB_ADDR + static_cast<uint32_t>(slot) * 0x1C;
  auto &ev = events[slot];
  if (!ev.used) {
    memoryPtr->WriteRAM32(addr + 0x00, 0);
    memoryPtr->WriteRAM32(addr + 0x04, EvCBStatusFree);
    memoryPtr->WriteRAM32(addr + 0x08, 0);
    memoryPtr->WriteRAM32(addr + 0x0C, 0);
    memoryPtr->WriteRAM32(addr + 0x10, 0);
    return;
  }
  memoryPtr->WriteRAM32(addr + 0x00, ev.classId);
  uint32_t status = EvCBStatusDisabled;
  if (ev.enabled)
    status = ev.fired ? EvCBStatusReady : EvCBStatusBusy;
  memoryPtr->WriteRAM32(addr + 0x04, status);
  memoryPtr->WriteRAM32(addr + 0x08, ev.spec);
  memoryPtr->WriteRAM32(addr + 0x0C, ev.mode);
  memoryPtr->WriteRAM32(addr + 0x10, ev.func);
}

void PS1HleBios::ReadEvCBFromRAM(int slot) {
  if (!memoryPtr || slot < 0 || slot >= MAX_EVENTS)
    return;
  uint32_t addr = EVCB_ADDR + static_cast<uint32_t>(slot) * 0x1C;
  uint32_t status = memoryPtr->ReadRAM32(addr + 0x04);
  auto &ev = events[slot];
  if (status == EvCBStatusFree) {
    ev.used = false;
    ev.enabled = false;
    ev.fired = false;
    return;
  }
  ev.used = true;
  ev.classId = memoryPtr->ReadRAM32(addr + 0x00);
  ev.spec = memoryPtr->ReadRAM32(addr + 0x08);
  ev.mode = memoryPtr->ReadRAM32(addr + 0x0C);
  ev.func = memoryPtr->ReadRAM32(addr + 0x10);
  ev.enabled = (status == EvCBStatusBusy || status == EvCBStatusReady);
  ev.fired = (status == EvCBStatusReady);
}

// ─── Event Delivery ─────────────────────────────────────────────────────

void PS1HleBios::DeliverEvent(uint32_t classId, uint32_t spec) {
  auto &log = AIO::Emulator::Common::Logger::Instance();
  bool traceEnabled = IsPs1HleTraceEnabled();
  bool anyMatch = false;
  for (int i = 0; i < MAX_EVENTS; i++) {
    auto &ev = events[i];
    if (ev.used && ev.enabled && ev.classId == classId && ev.spec == spec) {
      anyMatch = true;
      if (ev.mode == 0x1000 && ev.func != 0) {
        // Queue callback for execution after all event delivery completes.
        // Real BIOS calls these inline as subroutines during exception
        // handling.
        pendingCallbacks.push_back(ev.func);
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DeliverEvent: class=0x%08X spec=0x%04X → slot %d "
                     "callback=0x%08X (mode=0x1000, queued)",
                     classId, spec, i, ev.func);
        }
      } else {
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DeliverEvent: class=0x%08X spec=0x%04X → slot %d "
                     "FIRED (mode=0x%04X)",
                     classId, spec, i, ev.mode);
        }
        ev.fired = true;
        WriteEvCBToRAM(i);
      }
    }
  }
  if (traceEnabled && !anyMatch) {
    log.LogFmt(AIO::Emulator::Common::LogLevel::Debug, "PS1.HLE",
               "DeliverEvent: class=0x%08X spec=0x%04X → NO MATCH", classId,
               spec);
  }
}

// ─── BIOS Region ────────────────────────────────────────────────────────

void PS1HleBios::DeliverEventClass(uint32_t classId) {
  // Fire all enabled events with matching classId, regardless of spec.
  // Called on VBlank for library-defined event classes (e.g. 0xF0000011)
  // whose spec values are library-internal -- every registered slot fires
  // on the same hardware condition (VBlank).
  auto &log = AIO::Emulator::Common::Logger::Instance();
  bool traceEnabled = IsPs1HleTraceEnabled();
  for (int i = 0; i < MAX_EVENTS; i++) {
    auto &ev = events[i];
    if (ev.used && ev.enabled && ev.classId == classId) {
      if (ev.mode == 0x1000 && ev.func != 0) {
        pendingCallbacks.push_back(ev.func);
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DeliverEventClass: classId=0x%08X -> slot %d callback=0x%08X",
                     classId, i, ev.func);
        }
      } else {
        ev.fired = true;
        WriteEvCBToRAM(i);
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DeliverEventClass: classId=0x%08X -> slot %d FIRED (mode=0x%04X)",
                     classId, i, ev.mode);
        }
      }
    }
  }
}

void PS1HleBios::PopulateBiosRegion(PS1Memory &memory) {
  // Fill the BIOS region with JR $ra + NOP (harmless return stubs)
  // so any stray jump into BIOS doesn't crash on zeros
  for (uint32_t off = 0; off < MemSize::BIOS; off += 8) {
    WriteBIOSInstr(memory, off, JR_RA());
    WriteBIOSInstr(memory, off + 4, NOP());
  }

  // Boot exception vector at BIOS offset 0x180 (physical 0x1FC00180)
  // Install a stub that just does RFE + JR k0
  // k0 = $26, we load EPC into it
  // MFC0 $26, $14  (EPC)
  WriteBIOSInstr(memory, 0x180, MFC0(26, CPU::COP0::EPC));
  // JR $26
  WriteBIOSInstr(memory, 0x184, 0x03400008); // jr $26
  // RFE in the delay slot
  WriteBIOSInstr(memory, 0x188, RFE());
}

// ─── Kernel Stubs in RAM ────────────────────────────────────────────────

void PS1HleBios::InstallKernelStubs(PS1Memory &memory) {
  // ─── Halt stub at 0x00000000 ──────────────────────────────────────
  // On real hardware, exec() saves context and jalr's to the entry point.
  // If the game ever returns, exec() calls fatal() which halts.
  // TryHLETrap intercepts PC=0 before these execute, but write a
  // self-loop as a safety net: J 0x00000000 / NOP
  WriteRAMInstr(memory, 0x00, 0x08000000); // j 0x00000000
  WriteRAMInstr(memory, 0x04, NOP());
  WriteRAMInstr(memory, 0x08, NOP());
  WriteRAMInstr(memory, 0x0C, NOP());

  // RAM size in megabytes at [0x60]
  memory.WriteRAM32(0x60, 2);
  memory.WriteRAM32(0x64, 0x00000000);
  memory.WriteRAM32(0x68, 0x000000FF);

  // ─── Exception vector at 0x80000080 ───────────────────────────────
  // Real BIOS: 4 opcodes that jump to the kernel exception handler.
  // For HLE, the CPU intercepts PC=0x80 before fetching these.
  // Games that patch the exception handler expect the standard pattern
  // pointing to EXC_HANDLER_ADDR (real BIOS uses 0xC80).
  uint32_t excBase = 0x80;
  WriteRAMInstr(memory, excBase, LUI(26, 0x0000)); // lui k0, 0
  WriteRAMInstr(
      memory, excBase + 4,
      ADDIU(26, 26,
            static_cast<int16_t>(EXC_HANDLER_ADDR))); // addiu k0,k0,0xC80
  WriteRAMInstr(memory, excBase + 8, 0x03400008);     // jr k0
  WriteRAMInstr(memory, excBase + 12, NOP());

  // ─── Exception handler code block at EXC_HANDLER_ADDR ─────────────
  // Games read C(06h) and patch instructions at specific offsets from
  // the returned address. This block must contain the standard instruction
  // pattern the real BIOS has so games can verify and patch it.
  //
  // Real BIOS exception handler layout (from PSX-SPX patch docs):
  //   +00h: load ToT pointer and get TCB chain
  //   +08h..+24h: navigate to current TCB
  //   +28h: SW r1, [k0+04h]   ← start of register save block
  //   +2Ch: SW r2, [k0+08h]
  //   +30h: SW r3, [k0+0Ch]
  //   ...more register saves...
  //   +7Ch: SW ra, [k0+7Ch]
  //   +80h: MFC0 r3, cop0r14 (EPC)  ← lightgun patch target
  //   +84h: NOP
  //   ...rest of exception handler...
  //
  // We fill this with NOP except for the specific instructions that
  // games verify/patch.  HLE still intercepts at PC=0x80 so these
  // opcodes are never actually executed — they just need to be
  // readable at the expected addresses.

  // Zero-fill the whole handler block first
  for (uint32_t off = 0; off < EXC_HANDLER_SIZE; off += 4) {
    WriteRAMInstr(memory, EXC_HANDLER_ADDR + off, NOP());
  }

  // +00h: LUI k0, 0x0100 (part of ToT lookup — games verify this)
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x00, LUI(26, 0x0001));
  // +04h: NOP
  // +08h: LW k0, [k0+08h] — load ExCB pointer
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x08, 0x8F5A0008);
  // +0Ch: NOP
  // +10h: LW k0, [k0+00h] — first chain element
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x10, 0x8F5A0000);
  // +14h: NOP
  // +18h: ADDIU k0, k0, 8 — skip to TCB register save area
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x18, 0x235A0008);

  // +28h..+30h: Register saves that the cop0r13 patch targets
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x28, 0xAF410004); // SW r1,[k0+04h]
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x2C, 0xAF420008); // SW r2,[k0+08h]
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x30, 0xAF43000C); // SW r3,[k0+0Ch]

  // +7Ch: SW ra, [k0+7Ch]
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x7C, 0xAF5F007C);

  // +80h: MFC0 r3, cop0r14 (EPC) — lightgun patch writes here
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x80, 0x40037000);
  // +84h: NOP
  WriteRAMInstr(memory, EXC_HANDLER_ADDR + 0x84, NOP());

  // ─── A/B/C call vectors at 0xA0, 0xB0, 0xC0 ──────────────────────
  for (uint32_t tableAddr : {0xA0u, 0xB0u, 0xC0u}) {
    WriteRAMInstr(memory, tableAddr, JR_RA());
    WriteRAMInstr(memory, tableAddr + 4, NOP());
    WriteRAMInstr(memory, tableAddr + 8, NOP());
    WriteRAMInstr(memory, tableAddr + 12, NOP());
  }

  // Install a return stub in kernel memory that games can call
  // (used to fill jump table entries)
  WriteRAMInstr(memory, STUB_RET_ADDR, JR_RA());
  WriteRAMInstr(memory, STUB_RET_ADDR + 4, NOP());

  // Halt stub — infinite loop for when the game's main() returns.
  // Real BIOS has a shell loop; we just spin so the game doesn't
  // execute through address 0 and trigger false exceptions.
  uint32_t haltTarget = (0x80000000 | HALT_ADDR) >> 2;
  WriteRAMInstr(memory, HALT_ADDR, J(haltTarget));
  WriteRAMInstr(memory, HALT_ADDR + 4, NOP());

  // Callback return trampoline — mode=0x1000 callbacks return here.
  // TryHLETrap intercepts this before execution. Write a J-self loop
  // as a safety net in case it's ever reached without interception.
  uint32_t cbRetTarget = (0x80000000 | CALLBACK_RETURN_ADDR) >> 2;
  WriteRAMInstr(memory, CALLBACK_RETURN_ADDR, J(cbRetTarget));
  WriteRAMInstr(memory, CALLBACK_RETURN_ADDR + 4, NOP());
}

// ─── Trampoline Stubs ───────────────────────────────────────────────────
// Games call GetB0Table/GetC0Table and invoke functions via the pointers
// they find in those tables, bypassing the 0xB0/0xC0 vectors entirely.
// Each trampoline loads $t1 with the function index and jumps to the
// corresponding vector address where TryHLETrap intercepts the call.
//
// Per entry (12 bytes):
//   addiu $t1, $zero, <func>    — set function number
//   j     <vector>              — jump to 0xA0/0xB0/0xC0
//   nop                         — delay slot

void PS1HleBios::InstallTrampolines(PS1Memory &memory) {
  auto installBlock = [&](uint32_t baseAddr, uint32_t vectorAddr,
                          uint32_t count) {
    // J instruction target: vector is in KSEG0 (0x8000_00xx).
    // J uses {PC[31:28], target26 << 2}. Trampoline PC is in KSEG0 so
    // top nibble = 0x8. target26 = (0x80000000 | vectorAddr) >> 2.
    uint32_t jTarget = (0x80000000 | vectorAddr) >> 2;

    for (uint32_t i = 0; i < count; i++) {
      uint32_t addr = baseAddr + i * 12;
      // addiu $t1($9), $zero($0), i
      WriteRAMInstr(memory, addr, ADDIU(9, 0, static_cast<int16_t>(i)));
      // j vectorAddr
      WriteRAMInstr(memory, addr + 4, J(jTarget));
      // nop (delay slot)
      WriteRAMInstr(memory, addr + 8, NOP());
    }
  };

  installBlock(A0_TRAMPOLINE_ADDR, 0xA0, A0_TABLE_ENTRIES);
  installBlock(B0_TRAMPOLINE_ADDR, 0xB0, B0_TABLE_ENTRIES);
  installBlock(C0_TRAMPOLINE_ADDR, 0xC0, C0_TABLE_ENTRIES);

  // Diagnostic: verify first few trampolines were written correctly
  auto &log = AIO::Emulator::Common::Logger::Instance();
  for (uint32_t i = 0; i < 3; i++) {
    uint32_t addr = C0_TRAMPOLINE_ADDR + i * 12;
    uint32_t w0 = memory.Read32(0x80000000 | addr);
    uint32_t w1 = memory.Read32(0x80000000 | (addr + 4));
    uint32_t w2 = memory.Read32(0x80000000 | (addr + 8));
    uint32_t tableEntry = memory.Read32(0x80000000 | (C0_TABLE_ADDR + i * 4));
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "C0 trampoline[%u] @ 0x%X: %08X %08X %08X (table entry=0x%08X)",
               i, addr, w0, w1, w2, tableEntry);
  }
}

// ─── Kernel State Initialization ────────────────────────────────────────

static void WriteString(PS1Memory &memory, uint32_t addr, const char *str) {
  for (uint32_t i = 0; str[i] != '\0'; i++) {
    memory.Write8(0x80000000 | addr, static_cast<uint8_t>(str[i]));
  }
  memory.Write8(0x80000000 | (addr + static_cast<uint32_t>(std::strlen(str))),
                0);
}

void PS1HleBios::InitKernelState(PS1Memory &memory) {
  auto &log = AIO::Emulator::Common::Logger::Instance();

  // ─── A0 Jump Table at 0x200 (256 entries) ─────────────────────────
  // Each entry points to a trampoline that routes through the 0xA0 vector
  for (uint32_t i = 0; i < A0_TABLE_ENTRIES; i++) {
    uint32_t tramAddr = 0x80000000 | (A0_TRAMPOLINE_ADDR + i * 12);
    memory.WriteRAM32(A0_TABLE_ADDR + i * 4, tramAddr);
  }

  // ─── B0 Jump Table ────────────────────────────────────────────────
  b0TableRamAddr = B0_TABLE_ADDR;
  for (uint32_t i = 0; i < B0_TABLE_ENTRIES; i++) {
    uint32_t tramAddr = 0x80000000 | (B0_TRAMPOLINE_ADDR + i * 12);
    memory.WriteRAM32(B0_TABLE_ADDR + i * 4, tramAddr);
  }

  // ─── C0 Jump Table ────────────────────────────────────────────────
  c0TableRamAddr = C0_TABLE_ADDR;
  for (uint32_t i = 0; i < C0_TABLE_ENTRIES; i++) {
    uint32_t tramAddr = 0x80000000 | (C0_TRAMPOLINE_ADDR + i * 12);
    memory.WriteRAM32(C0_TABLE_ADDR + i * 4, tramAddr);
  }

  // C(06h) = ExceptionHandler — games read this to patch exception handling.
  // Must point to the exception handler code block (real BIOS uses 0xC80).
  memory.WriteRAM32(C0_TABLE_ADDR + 0x06 * 4, 0x80000000 | EXC_HANDLER_ADDR);

  // ─── Zero-fill all control block regions ──────────────────────────
  uint8_t *ram = memory.GetRAMPointer();
  std::memset(ram + EXCB_ADDR, 0, 0x20);
  std::memset(ram + PCB_ADDR, 0, 0x04);
  std::memset(ram + TCB_ADDR, 0, 0x300);
  std::memset(ram + EVCB_ADDR, 0, 0x1C0);
  std::memset(ram + FCB_ADDR, 0, 0x2C0);
  std::memset(ram + DCB_ADDR, 0, 0x320);

  // ─── Kernel Heap ───────────────────────────────────────────────────
  // Initialize the kernel heap so alloc_kernel_memory (B0:00h) works.
  // The real BIOS calls SysInitMemory during boot; we do it here.
  kernelHeapBase = KERNEL_HEAP_ADDR;
  kernelHeapSize = KERNEL_HEAP_SIZE;
  kernelHeapPtr = KERNEL_HEAP_ADDR;
  std::memset(ram + KERNEL_HEAP_ADDR, 0, KERNEL_HEAP_SIZE);

  // ─── Handler Chain Array ──────────────────────────────────────────
  // Allocate from kernel heap: 4 priorities × 8 bytes (first_ptr + padding)
  handlersArrayAddr = kernelHeapPtr;
  kernelHeapPtr += NUM_HANDLER_PRIORITIES * 8;
  std::memset(ram + handlersArrayAddr, 0, NUM_HANDLER_PRIORITIES * 8);

  // ─── Table of Tables at 0x100-0x157 ───────────────────────────────
  // ToT[0x00] = handlersArray (real BIOS: linked-list heads per priority)
  // ToT[0x08] = PCB, ToT[0x10] = TCB, ToT[0x20] = EvCB, etc.
  memory.WriteRAM32(0x100, 0x80000000 | handlersArrayAddr); // handlersArray
  memory.WriteRAM32(0x104, NUM_HANDLER_PRIORITIES * 0x08);  // handlersArray sz
  memory.WriteRAM32(0x108, 0x80000000 | PCB_ADDR);          // PCB base
  memory.WriteRAM32(0x10C, 1 * 0x04);                       // PCB size
  memory.WriteRAM32(0x110, 0x80000000 | TCB_ADDR);          // TCB base
  memory.WriteRAM32(0x114, 4 * 0xC0);                       // TCB size
  memory.WriteRAM32(0x118, 0);                              // unused
  memory.WriteRAM32(0x11C, 0);                              // unused
  memory.WriteRAM32(0x120, 0x80000000 | EVCB_ADDR);         // EvCB base
  memory.WriteRAM32(0x124, 16 * 0x1C);                      // EvCB size
  memory.WriteRAM32(0x128, 0);                              // unused
  memory.WriteRAM32(0x12C, 0);                              // unused
  memory.WriteRAM32(0x130, 0);                              // unused
  memory.WriteRAM32(0x134, 0);                              // unused
  memory.WriteRAM32(0x138, 0);                              // unused
  memory.WriteRAM32(0x13C, 0);                              // unused
  memory.WriteRAM32(0x140, 0x80000000 | FCB_ADDR);          // FCB base
  memory.WriteRAM32(0x144, 16 * 0x2C);                      // FCB size
  memory.WriteRAM32(0x148, 0);                              // unused
  memory.WriteRAM32(0x14C, 0);                              // unused
  memory.WriteRAM32(0x150, 0x80000000 | DCB_ADDR);          // DCB base
  memory.WriteRAM32(0x154, 10 * 0x50);                      // DCB size

  // ─── Device Name Strings ──────────────────────────────────────────
  uint32_t strOff = DEV_STRINGS_ADDR;
  uint32_t ttyNameAddr = strOff;
  WriteString(memory, strOff, "tty");
  strOff += 4; // "tty\0"

  uint32_t ttyLongNameAddr = strOff;
  WriteString(memory, strOff, "CONSOLE");
  strOff += 8; // "CONSOLE\0"

  uint32_t cdromNameAddr = strOff;
  WriteString(memory, strOff, "cdrom");
  strOff += 8; // "cdrom\0" padded

  uint32_t cdromLongNameAddr = strOff;
  WriteString(memory, strOff, "CD-ROM");
  strOff += 8;

  uint32_t buNameAddr = strOff;
  WriteString(memory, strOff, "bu");
  strOff += 4;

  uint32_t buLongNameAddr = strOff;
  WriteString(memory, strOff, "MEMORY CARD");
  strOff += 12;

  // ─── DCB: Device Control Blocks ───────────────────────────────────
  // DCB function pointers point to the stub return (JR $ra; NOP) since
  // device I/O is handled at the HLE level, not via MIPS function calls.
  uint32_t stubAddr = 0x80000000 | STUB_RET_ADDR;

  // DCB[0] = TTY (dummy, flags=1 for no-DUART)
  uint32_t dcb0 = DCB_ADDR;
  memory.WriteRAM32(dcb0 + 0x00, 0x80000000 | ttyNameAddr); // short name
  memory.WriteRAM32(dcb0 + 0x04, 0x01);                     // flags: dummy tty
  memory.WriteRAM32(dcb0 + 0x08, 0x01);                     // sector size
  memory.WriteRAM32(dcb0 + 0x0C, 0x80000000 | ttyLongNameAddr); // long name
  // Function pointers (init, open, in_out, etc.) — point to stub
  for (uint32_t off = 0x10; off < 0x50; off += 4) {
    memory.WriteRAM32(dcb0 + off, stubAddr);
  }

  // DCB[1] = CDROM
  uint32_t dcb1 = DCB_ADDR + 0x50;
  memory.WriteRAM32(dcb1 + 0x00, 0x80000000 | cdromNameAddr);
  memory.WriteRAM32(dcb1 + 0x04, 0x14);  // flags
  memory.WriteRAM32(dcb1 + 0x08, 0x800); // sector size 2048
  memory.WriteRAM32(dcb1 + 0x0C, 0x80000000 | cdromLongNameAddr);
  for (uint32_t off = 0x10; off < 0x50; off += 4) {
    memory.WriteRAM32(dcb1 + off, stubAddr);
  }

  // DCB[2] = Memory Card (bu)
  uint32_t dcb2 = DCB_ADDR + 0xA0;
  memory.WriteRAM32(dcb2 + 0x00, 0x80000000 | buNameAddr);
  memory.WriteRAM32(dcb2 + 0x04, 0x14); // flags
  memory.WriteRAM32(dcb2 + 0x08, 0x80); // sector size 128
  memory.WriteRAM32(dcb2 + 0x0C, 0x80000000 | buLongNameAddr);
  for (uint32_t off = 0x10; off < 0x50; off += 4) {
    memory.WriteRAM32(dcb2 + off, stubAddr);
  }

  // ─── FCB: Pre-open stdin (fd=0) and stdout (fd=1) as TTY ─────────
  // FCB format: [status, disk_id, xfer_addr, xfer_len, fpos,
  //              dev_flags, error, DCB_ptr, filesize, LBN, fcb_num]
  uint32_t dcb0Kseg = 0x80000000 | dcb0;

  // fd=0 (stdin) — access mode = READ (0x01)
  uint32_t fcb0 = FCB_ADDR;
  memory.WriteRAM32(fcb0 + 0x00, 0x01);     // status/accessmode = READ
  memory.WriteRAM32(fcb0 + 0x14, 0x01);     // device flags (from DCB)
  memory.WriteRAM32(fcb0 + 0x1C, dcb0Kseg); // pointer to DCB[0] (TTY)
  memory.WriteRAM32(fcb0 + 0x28, 0x00);     // FCB number

  // fd=1 (stdout) — access mode = WRITE (0x02)
  uint32_t fcb1 = FCB_ADDR + 0x2C;
  memory.WriteRAM32(fcb1 + 0x00, 0x02);     // status/accessmode = WRITE
  memory.WriteRAM32(fcb1 + 0x14, 0x01);     // device flags
  memory.WriteRAM32(fcb1 + 0x1C, dcb0Kseg); // pointer to DCB[0] (TTY)
  memory.WriteRAM32(fcb1 + 0x28, 0x01);     // FCB number

  // fd=2 (stderr) — same as stdout
  uint32_t fcb2 = FCB_ADDR + 0x2C * 2;
  memory.WriteRAM32(fcb2 + 0x00, 0x02);
  memory.WriteRAM32(fcb2 + 0x14, 0x01);
  memory.WriteRAM32(fcb2 + 0x1C, dcb0Kseg);
  memory.WriteRAM32(fcb2 + 0x28, 0x02);

  // ─── TCB: Mark thread 0 as active ────────────────────────────────
  // TCB[0].status = 0x4000 (Used TCB)
  memory.WriteRAM32(TCB_ADDR + 0x00, 0x4000);

  // ─── PCB: Point to the current thread (TCB[0]) ───────────────────
  memory.WriteRAM32(PCB_ADDR, 0x80000000 | TCB_ADDR);

  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "Kernel state initialized: ToT=0x100 FCB=0x%X DCB=0x%X "
             "TCB=0x%X PCB=0x%X A0=0x%X B0=0x%X C0=0x%X",
             FCB_ADDR, DCB_ADDR, TCB_ADDR, PCB_ADDR, A0_TABLE_ADDR,
             B0_TABLE_ADDR, C0_TABLE_ADDR);
}

// ─── GPU Initialization ─────────────────────────────────────────────────

void PS1HleBios::InitGPU(PS1GPU &gpu) {
  gpu.Reset();
  // GP1(03h) display enable — bit 0 = 0 means display ON.
  // Without this, the screen stays black even after the game renders.
  gpu.WriteGP1(0x03000000);
}

void PS1HleBios::InitCDROM(CDROM &cdrom) {
  // The real BIOS boot sequence initializes the CDROM controller by writing
  // to its hardware registers. Games expect interruptEnable=0x1F (all five
  // interrupt types enabled) before they send their first command.
  // Replicate those register writes so the first CdlGetStat fires an IRQ.
  cdrom.Write8(IO::CDROM_BASE + 0, 0x01); // Select index 1
  cdrom.Write8(IO::CDROM_BASE + 2, 0x1F); // reg2.idx1 = interruptEnable = all
  cdrom.Write8(IO::CDROM_BASE + 0, 0x00); // Restore index 0

  // Real BIOS _96_init opens 5 internal CDROM events (mode=0x2000, no
  // callback). The game's CD library calls DeliverEvent/TestEvent on these to
  // track command completion. Without them, DeliverEvent finds no match and the
  // game's state machine never advances.
  static constexpr uint32_t cdSpecs[] = {0x0010, 0x0020, 0x0040, 0x0080,
                                         0x8000};
  auto &log = AIO::Emulator::Common::Logger::Instance();
  for (uint32_t spec : cdSpecs) {
    int slot = -1;
    for (int i = 0; i < MAX_EVENTS; i++) {
      if (!events[i].used) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      events[slot] = {0xF0000003, spec, 0x2000, 0, true, true, false};
      WriteEvCBToRAM(slot);
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "InitCDROM: opened internal event slot %d "
                 "class=0xF0000003 spec=0x%04X mode=0x2000",
                 slot, spec);
    }
  }
}

// ─── EXE Loading from Disc Image ───────────────────────────────────────

// Find a file in an ISO9660 directory by name prefix (case-insensitive),
// returns its starting sector and size. Searches raw BIN disc sectors.
static bool FindFileInDirectory(CDROM &cdrom, uint32_t dirSector,
                                uint32_t dirSize, const char *namePrefix,
                                uint32_t &outSector, uint32_t &outSize) {
  uint8_t dirBuf[CDROM::SECTOR_DATA_SIZE];
  uint32_t sectorsInDir =
      (dirSize + CDROM::SECTOR_DATA_SIZE - 1) / CDROM::SECTOR_DATA_SIZE;

  for (uint32_t s = 0; s < sectorsInDir; s++) {
    if (!cdrom.ReadSectorData(dirSector + s, dirBuf))
      return false;

    uint32_t pos = 0;
    while (pos < CDROM::SECTOR_DATA_SIZE) {
      uint8_t recLen = dirBuf[pos];
      if (recLen == 0)
        break;
      uint8_t nameLen = dirBuf[pos + 32];
      if (nameLen > 0 && nameLen < 128) {
        // Compare case-insensitively against the prefix
        const char *entryName =
            reinterpret_cast<const char *>(&dirBuf[pos + 33]);
        size_t prefixLen = std::strlen(namePrefix);
        bool match = (nameLen >= prefixLen);
        for (size_t i = 0; i < prefixLen && match; i++) {
          char a = entryName[i];
          char b = namePrefix[i];
          if (a >= 'a' && a <= 'z')
            a -= 32;
          if (b >= 'a' && b <= 'z')
            b -= 32;
          if (a != b)
            match = false;
        }
        if (match) {
          uint32_t extent = 0;
          std::memcpy(&extent, &dirBuf[pos + 2], 4);
          uint32_t size = 0;
          std::memcpy(&size, &dirBuf[pos + 10], 4);
          outSector = extent;
          outSize = size;
          return true;
        }
      }
      pos += recLen;
    }
  }
  return false;
}

// Parse SYSTEM.CNF to extract the boot EXE filename
static bool ParseSystemCnf(const uint8_t *data, uint32_t size, char *outName,
                           uint32_t outNameSize) {
  // Look for "cdrom:" or "cdrom:\\" prefix, then extract filename
  const char *text = reinterpret_cast<const char *>(data);
  const char *end = text + size;
  const char *bootLine = nullptr;

  for (const char *p = text; p < end - 6; p++) {
    if (std::strncmp(p, "cdrom:", 6) == 0 ||
        std::strncmp(p, "CDROM:", 6) == 0) {
      bootLine = p + 6;
      break;
    }
  }
  if (!bootLine)
    return false;

  // Skip backslashes
  while (bootLine < end && (*bootLine == '\\' || *bootLine == '/'))
    bootLine++;

  // Extract filename until ';' or whitespace
  uint32_t i = 0;
  while (bootLine + i < end && i < outNameSize - 1 && bootLine[i] != ';' &&
         bootLine[i] != '\r' && bootLine[i] != '\n' && bootLine[i] != ' ') {
    outName[i] = bootLine[i];
    i++;
  }
  outName[i] = '\0';
  return i > 0;
}

bool PS1HleBios::FindAndLoadExe(PS1Memory &memory, CDROM &cdrom, R3000A &cpu,
                                PS1GPU &gpu) {
  auto &log = AIO::Emulator::Common::Logger::Instance();
  if (!cdrom.HasDisc()) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "No disc loaded");
    return false;
  }

  // Step 1: Read Primary Volume Descriptor from sector 16 (ISO9660)
  uint8_t pvdBuf[CDROM::SECTOR_DATA_SIZE];
  if (!cdrom.ReadSectorData(16, pvdBuf)) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "Failed to read PVD at sector 16");
    return false;
  }

  // Validate ISO9660 magic "CD001" at offset 1
  if (std::memcmp(&pvdBuf[1], "CD001", 5) != 0) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "PVD magic mismatch (not CD001)");
    return false;
  }

  // Root directory record at PVD offset 156
  uint32_t rootSector = 0, rootSize = 0;
  std::memcpy(&rootSector, &pvdBuf[156 + 2], 4);
  std::memcpy(&rootSize, &pvdBuf[156 + 10], 4);
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "ISO9660: root dir sector=%u size=%u", rootSector, rootSize);

  // Step 2: Find and parse SYSTEM.CNF from the root directory
  uint32_t cnfSector = 0, cnfSize = 0;
  if (!FindFileInDirectory(cdrom, rootSector, rootSize, "SYSTEM.CNF", cnfSector,
                           cnfSize)) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "SYSTEM.CNF not found in root directory");
    return false;
  }
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "SYSTEM.CNF at sector=%u size=%u", cnfSector, cnfSize);

  // Read SYSTEM.CNF content (usually fits in one sector)
  uint8_t cnfBuf[CDROM::SECTOR_DATA_SIZE];
  if (!cdrom.ReadSectorData(cnfSector, cnfBuf))
    return false;
  if (cnfSize > CDROM::SECTOR_DATA_SIZE)
    cnfSize = CDROM::SECTOR_DATA_SIZE;

  // Extract boot EXE filename from SYSTEM.CNF
  char exeFilename[128];
  if (!ParseSystemCnf(cnfBuf, cnfSize, exeFilename, sizeof(exeFilename))) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "Failed to parse boot filename from SYSTEM.CNF");
    return false;
  }
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE", "Boot EXE: %s",
             exeFilename);

  // Step 3: Find the boot EXE in the root directory
  uint32_t exeSector = 0, exeFileSize = 0;
  if (!FindFileInDirectory(cdrom, rootSector, rootSize, exeFilename, exeSector,
                           exeFileSize)) {
    log.LogFmt(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
               "Boot EXE '%s' not found in root directory", exeFilename);
    return false;
  }
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "EXE file at sector=%u size=%u", exeSector, exeFileSize);

  // Step 4: Read the PS-X EXE header from the first sector of the file
  uint8_t headerBuf[CDROM::SECTOR_DATA_SIZE];
  if (!cdrom.ReadSectorData(exeSector, headerBuf))
    return false;

  PSXExeHeader header;
  std::memcpy(&header, headerBuf, sizeof(header));

  if (std::memcmp(header.magic, "PS-X EXE", 8) != 0) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "PS-X EXE magic mismatch");
    return false;
  }
  if (header.destAddr == 0 || header.fileSize == 0) {
    log.Log(AIO::Emulator::Common::LogLevel::Error, "PS1.HLE",
            "Invalid EXE header (destAddr or fileSize is 0)");
    return false;
  }

  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "PS-X EXE: PC=0x%08X dest=0x%08X size=%u GP=0x%08X SP=0x%08X",
             header.initialPC, header.destAddr, header.fileSize,
             header.initialGP, header.initialSP);

  // Step 5: Read EXE code data sector-by-sector from (exeSector + 1)
  // The header occupies 1 sector (2048 bytes), code starts at next sector
  uint32_t codeSector = exeSector + 1;
  uint32_t bytesRemaining = header.fileSize;
  uint32_t destPhys = header.destAddr & 0x1FFFFF;
  uint8_t *ramPtr = memory.GetRAMPointer();

  while (bytesRemaining > 0 && destPhys < MemSize::RAM) {
    uint8_t sectorBuf[CDROM::SECTOR_DATA_SIZE];
    if (!cdrom.ReadSectorData(codeSector, sectorBuf))
      break;

    uint32_t chunkSize = (bytesRemaining < CDROM::SECTOR_DATA_SIZE)
                             ? bytesRemaining
                             : CDROM::SECTOR_DATA_SIZE;
    uint32_t available = MemSize::RAM - destPhys;
    if (chunkSize > available)
      chunkSize = available;

    std::memcpy(ramPtr + destPhys, sectorBuf, chunkSize);
    destPhys += chunkSize;
    bytesRemaining -= chunkSize;
    codeSector++;
  }

  // Step 6: Zero-fill BSS if specified (usually redundant after full RAM clear)
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "EXE BSS: memfillStart=0x%08X memfillSize=0x%08X",
             header.memfillStart, header.memfillSize);
  if (header.memfillSize > 0 && header.memfillStart != 0) {
    uint32_t bssPhys = header.memfillStart & 0x1FFFFF;
    uint32_t bssEnd = bssPhys + header.memfillSize;
    if (bssEnd > MemSize::RAM)
      bssEnd = MemSize::RAM;
    if (bssPhys < bssEnd) {
      std::memset(ramPtr + bssPhys, 0, bssEnd - bssPhys);
    }
  }

  // Step 7: Set CPU initial state from EXE header
  cpu.SetPC(header.initialPC);

  // Real BIOS calls the EXE entry via JAL, setting $ra to the BIOS shell loop.
  // Set $ra to our halt stub so that if the game's main() returns,
  // it spins harmlessly instead of executing through address 0.
  cpu.SetRegister(31, 0x80000000 | HALT_ADDR);

  if (header.initialGP != 0) {
    cpu.SetRegister(28, header.initialGP);
  }

  uint32_t sp = header.initialSP;
  if (sp == 0)
    sp = 0x801FFF00;
  sp += header.spOffset;
  cpu.SetRegister(29, sp);
  cpu.SetRegister(30, sp);

  // Enable COP0 status: BEV=0, COP0+COP2 usable, interrupts enabled, hw IRQ
  // line unmasked
  cpu.SetCOP0(CPU::COP0::SR,
              CPU::SR::CU0 | CPU::SR::CU2 | CPU::SR::IEc | (1u << 10));

  return true;
}

// ─── Instruction Writers ────────────────────────────────────────────────

void PS1HleBios::WriteBIOSInstr(PS1Memory &memory, uint32_t physOffset,
                                uint32_t instr) {
  memory.WriteBIOS32(physOffset, instr);
}

void PS1HleBios::WriteRAMInstr(PS1Memory &memory, uint32_t offset,
                               uint32_t instr) {
  memory.WriteRAM32(offset, instr);
}

// ─── Mode=0x1000 Callback Continuation ──────────────────────────────────

void PS1HleBios::ResumeAfterCallback(PS1 &ps1) {
  auto &cpu = ps1.GetCPU();
  if (IsPs1HleTraceEnabled()) {
    auto &log = AIO::Emulator::Common::Logger::Instance();
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "ResumeAfterCallback entered, CPU ra=0x%08X SP=0x%08X",
               cpu.GetRegister(31), cpu.GetRegister(29));
  }
  DispatchNextCallbackOrResume(ps1);
}

void PS1HleBios::DispatchNextCallbackOrResume(PS1 &ps1) {
  auto &cpu = ps1.GetCPU();
  auto &log = AIO::Emulator::Common::Logger::Instance();
  bool traceEnabled = IsPs1HleTraceEnabled();

  // If more callbacks are queued, dispatch the next one
  if (!pendingCallbacks.empty()) {
    uint32_t cbAddr = pendingCallbacks.front();
    pendingCallbacks.erase(pendingCallbacks.begin());

    uint32_t trampoline = 0x80000000 | CALLBACK_RETURN_ADDR;
    cpu.SetRegister(31, trampoline);
    cpu.SetPC(cbAddr);
    if (traceEnabled) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "DispatchNextCB: dispatching next callback 0x%08X", cbAddr);
    }
    return;
  }

  // All callbacks executed — resume exception handling from the priority 1
  // handler phase. Re-read I_STAT/I_MASK since callbacks may have acked IRQs.
  auto &irqs = ps1.GetInterrupts();
  auto &mem = ps1.GetMemory();

  uint32_t istat = irqs.ReadStat();
  uint32_t imask = irqs.ReadMask();
  uint32_t pending = istat & imask;

  // Priority 1: auto-ack timer/VBlank IRQs
  {
    struct RCntEntry {
      uint32_t irqBit;
      uint32_t rootCounterClass;
      int flagIndex;
    };
    static constexpr RCntEntry rcntTable[] = {
        {IRQ::TIMER0, 0xF2000000, 0},
        {IRQ::TIMER1, 0xF2000001, 1},
        {IRQ::TIMER2, 0xF2000002, 2},
        {IRQ::VBLANK, 0xF2000003, 3},
    };

    uint32_t ackedBits = 0;
    for (auto &rc : rcntTable) {
      if (!(pending & rc.irqBit))
        continue;

      // Only auto-ack when changeClearRCntFlags says to.
      // When flag is 0, the game's longjmp handler owns acknowledgment.
      if (changeClearRCntFlags[rc.flagIndex] != 0) {
        ackedBits |= rc.irqBit;
      }
    }

    if (ackedBits != 0) {
      irqs.ClearIRQ(ackedBits);
      pending = irqs.ReadStat() & imask;

      if (pending == 0) {
        // All IRQs handled by callbacks + priority 1 auto-ack — fast return.
        // Restore CPU state captured at HandleException entry, NOT from TCB
        // in RAM (decompressor mirror writes corrupt both PCB_ADDR and TCB).
        for (int i = 1; i < 32; i++) {
          if (i == 26)
            continue;
          cpu.SetRegister(i, savedRegs[i]);
        }

        uint32_t sr = (savedSR & ~0xF) | ((savedSR >> 2) & 0xF);
        cpu.SetHI(savedHI);
        cpu.SetLO(savedLO);
        cpu.SetCOP0(CPU::COP0::SR, sr);
        cpu.SetPC(savedEpc);
        inExceptionHandler = false;
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DispatchNextCB: fast return to EPC=0x%08X SR=0x%08X "
                     "ackedBits=0x%04X",
                     savedEpc, sr, ackedBits);
        }
        return;
      }
    }
  }

  // If nothing is pending after priority-table pass, fast-return to savedEpc.
  // This handles the case where VBlank was pre-cleared in HandleException (via
  // deliveredIrqs) before the callbacks ran: pending=0, ackedBits=0, but we
  // must NOT longjmp — that would abandon VSync()'s polling-loop stack frame.
  // Instead, restore game registers and return so the game can see the updated
  // counter.
  if (pending == 0) {
    for (int i = 1; i < 32; i++) {
      if (i == 26)
        continue;
      cpu.SetRegister(i, savedRegs[i]);
    }
    uint32_t sr = (savedSR & ~0xF) | ((savedSR >> 2) & 0xF);
    cpu.SetHI(savedHI);
    cpu.SetLO(savedLO);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(savedEpc);
    inExceptionHandler = false;
    if (traceEnabled) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "DispatchNextCB: all handled, fast return to EPC=0x%08X",
                 savedEpc);
    }
    return;
  }

  // Still pending IRQs — dispatch to handler chain or longjmp
  if (traceEnabled) {
    log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
               "DispatchNextCB: still pending=0x%04X istat=0x%04X imask=0x%04X",
               pending, istat, imask);
  }

  if (handlersArrayAddr != 0) {
    uint32_t chainHead = mem.Read32(0x80000000 | handlersArrayAddr);
    uint32_t entry = chainHead;
    while (entry != 0) {
      uint32_t func2 = mem.Read32(entry + 0x04);
      if (func2 != 0) {
        // inExceptionHandler already true
        uint32_t rfeTrampoline = 0x80000000 | (B0_TRAMPOLINE_ADDR + 0x17 * 12);
        cpu.SetRegister(31, rfeTrampoline);
        cpu.SetPC(func2);
        if (traceEnabled) {
          log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                     "DispatchNextCB: handler chain func=0x%08X ra=0x%08X",
                     func2, rfeTrampoline);
        }
        return;
      }
      entry = mem.Read32(entry + 0x00);
    }
  }

  if (hookedEntryIntHandler != 0) {
    uint32_t sjBuf = hookedEntryIntHandler;
    uint32_t ra = mem.Read32(sjBuf + 0x00);
    uint32_t sp = mem.Read32(sjBuf + 0x04);

    // inExceptionHandler already true
    cpu.SetRegister(29, sp);
    cpu.SetRegister(30, mem.Read32(sjBuf + 0x08));
    for (int i = 0; i < 8; i++) {
      cpu.SetRegister(16 + i, mem.Read32(sjBuf + 0x0C + i * 4));
    }
    cpu.SetRegister(28, mem.Read32(sjBuf + 0x2C));
    cpu.SetRegister(2, 1);
    // longjmp completes exception handling — clear guard so future exceptions
    // (VBlank counter increments, CDROM, DMA) are processed normally.
    inExceptionHandler = false;
    cpu.SetPC(ra);
    if (traceEnabled) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "DispatchNextCB: longjmp to ra=0x%08X sp=0x%08X", ra, sp);
    }
    return;
  }

  // No handler registered — just return from exception using saved state
  {
    for (int i = 1; i < 32; i++) {
      if (i == 26)
        continue;
      cpu.SetRegister(i, savedRegs[i]);
    }
    uint32_t sr = (savedSR & ~0xF) | ((savedSR >> 2) & 0xF);
    cpu.SetHI(savedHI);
    cpu.SetLO(savedLO);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(savedEpc);
    inExceptionHandler = false;
    if (traceEnabled) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "DispatchNextCB: fallback RFE to EPC=0x%08X", savedEpc);
    }
  }
}

} // namespace AIO::Emulator::PS1
