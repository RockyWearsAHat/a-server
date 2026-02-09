#include "emulator/ps1/PS1HleBios.h"
#include "emulator/common/Logger.h"
#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/PS1.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/R3000A.h"
#include <cstring>

namespace AIO::Emulator::PS1 {

// ─── Public Entry Point ─────────────────────────────────────────────────

bool PS1HleBios::InitHLE(PS1 &ps1) {
  auto &memory = ps1.GetMemory();
  auto &cpu = ps1.GetCPU();
  auto &gpu = ps1.GetGPU();
  auto &cdrom = ps1.GetCDROM();

  ResetState();
  PopulateBiosRegion(memory);
  InstallKernelStubs(memory);
  InitGPU(gpu);

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
  hleSeed = 0;
}

// ─── HLE Exception Handler ──────────────────────────────────────────────

void PS1HleBios::HandleException(PS1 &ps1) {
  auto &cpu = ps1.GetCPU();
  auto &irqs = ps1.GetInterrupts();

  uint32_t cause = cpu.GetCause();
  uint32_t excCode = (cause >> 2) & 0x1F;

  if (excCode == 0 && hookedEntryIntHandler != 0) {
    uint32_t istat = irqs.ReadStat();
    uint32_t imask = irqs.ReadMask();
    uint32_t pending = istat & imask;

    // Save full CPU state so ReturnFromException can restore it
    for (int i = 0; i < 32; i++) {
      savedFrame.gpr[i] = cpu.GetRegister(i);
    }
    savedFrame.hi = cpu.GetHI();
    savedFrame.lo = cpu.GetLO();
    savedFrame.valid = true;

    if (pending & IRQ::VBLANK) {
      DeliverEvent(0xF0000001, 0x0001);
      irqs.WriteStat(istat & ~IRQ::VBLANK);
    }
    if (pending & IRQ::TIMER0) {
      DeliverEvent(0xF0000002, 0x0001);
      irqs.WriteStat(istat & ~IRQ::TIMER0);
    }
    if (pending & IRQ::TIMER1) {
      DeliverEvent(0xF0000002, 0x0002);
      irqs.WriteStat(istat & ~IRQ::TIMER1);
    }
    if (pending & IRQ::TIMER2) {
      DeliverEvent(0xF0000002, 0x0004);
      irqs.WriteStat(istat & ~IRQ::TIMER2);
    }
    if (pending & IRQ::CDROM) {
      irqs.WriteStat(istat & ~IRQ::CDROM);
    }
    if (pending & IRQ::DMA) {
      irqs.WriteStat(istat & ~IRQ::DMA);
    }
    if (pending & IRQ::SIO0) {
      irqs.WriteStat(istat & ~IRQ::SIO0);
    }

    // longjmp into the setjmp buffer registered via HookEntryInt (B0:19h).
    auto &mem = ps1.GetMemory();
    uint32_t sjBuf = hookedEntryIntHandler;
    uint32_t ra = mem.Read32(sjBuf + 0x00);
    uint32_t sp = mem.Read32(sjBuf + 0x04);

    {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      char dbg[256];
      snprintf(dbg, sizeof(dbg),
               "ExcDispatch: longjmp buf=0x%08X ra=0x%08X sp=0x%08X EPC=0x%08X "
               "SR=0x%08X",
               sjBuf, ra, sp, cpu.GetEPC(), cpu.GetCOP0(CPU::COP0::SR));
      log.Log(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE", dbg);
    }

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

  if (excCode == 0) {
    // Hardware interrupt but no registered handler — just acknowledge and
    // return
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
    // SYSCALL — skip the instruction and RFE
    uint32_t epc = cpu.GetEPC();
    cpu.SetPC(epc + 4);
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    return;
  }

  // Default: RFE and return to EPC
  uint32_t epc = cpu.GetEPC();
  cpu.SetPC(epc);
  uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
  sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
  cpu.SetCOP0(CPU::COP0::SR, sr);
}

// ─── HLE BIOS Dispatch ─────────────────────────────────────────────────

void PS1HleBios::Dispatch(PS1 &ps1, uint8_t table, uint8_t func) {
  auto &log = AIO::Emulator::Common::Logger::Instance();
  log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
             "BIOS call: table=0x%02X func=0x%02X ($ra=0x%08X)", table, func,
             ps1.GetCPU().GetRegister(31));

  switch (table) {
  case 0xA0:
    DispatchA0(ps1, func);
    break;
  case 0xB0:
    DispatchB0(ps1, func);
    break;
  case 0xC0:
    DispatchC0(ps1, func);
    break;
  }
}

// ─── A-Table (0xA0) ─────────────────────────────────────────────────────

void PS1HleBios::DispatchA0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();
  auto &gpu = ps1.GetGPU();
  auto &mem = ps1.GetMemory();

  switch (func) {

  // A0:2Ah memcpy(dst, src, len)
  case 0x2A: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    cpu.SetRegister(2, dst);
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
    break;
  }

  // A0:2Ch memcmp(s1, s2, len)
  case 0x2C: {
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

  // A0:33h strlen(s)
  case 0x33: {
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

  // A0:2Dh bcopy(src, dst, len)
  case 0x2D: {
    uint32_t src = cpu.GetRegister(4);
    uint32_t dst = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    break;
  }

  // A0:2Eh rand()
  case 0x2E: {
    hleSeed = hleSeed * 1103515245 + 12345;
    cpu.SetRegister(2, (hleSeed >> 16) & 0x7FFF);
    break;
  }

  // A0:2Fh srand(seed)
  case 0x2F: {
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

  // A0:3Ah malloc(size) — simple bump allocator
  case 0x3A: {
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

  // A0:3Bh free(ptr) — no-op for bump allocator
  case 0x3B:
    break;

  // A0:3Ch std_out_putchar — silently consume
  case 0x3C:
    break;

  // A0:3Fh printf — silently consume
  case 0x3F:
    break;

  // A0:44h FlushCache — no-op for HLE
  case 0x44:
    break;

  // A0:47h GPU_SendGP1Command(cmd)
  case 0x47: {
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

  // A0:70h _bu_init — no-op
  case 0x70:
    break;

  // A0:72h _96_remove — broken in real BIOS, safe no-op
  case 0x72:
    break;

  default:
    cpu.SetRegister(2, 0);
    break;
  }
}

// ─── B-Table (0xB0) ─────────────────────────────────────────────────────

void PS1HleBios::DispatchB0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();
  auto &gpu = ps1.GetGPU();

  switch (func) {

  // B0:08h GPU_cw(cmd) — send GP1 command
  case 0x08: {
    gpu.WriteGP1(cpu.GetRegister(4));
    break;
  }

  // B0:09h GPU_cwp(addr, count) — send GP0 words
  case 0x09: {
    auto &mem = ps1.GetMemory();
    uint32_t addr = cpu.GetRegister(4);
    uint32_t count = cpu.GetRegister(5);
    if (count > 0x40000)
      count = 0x40000;
    for (uint32_t i = 0; i < count; i++) {
      gpu.WriteGP0(mem.Read32(addr + i * 4));
    }
    break;
  }

  // B0:0Ah send_gpu_linked_list (same as A0:4Bh)
  case 0x0A: {
    auto &mem = ps1.GetMemory();
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

  // B0:0Bh GPU_status — read GPUSTAT
  case 0x0B: {
    cpu.SetRegister(2, gpu.ReadGPUSTAT());
    break;
  }

  // B0:17h ReturnFromException — restore full CPU state saved at exception
  // entry
  case 0x17: {
    uint32_t epc = cpu.GetEPC();

    {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      char dbg[256];
      uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
      snprintf(dbg, sizeof(dbg),
               "ReturnFromException: EPC=0x%08X SR=0x%08X valid=%d", epc, sr,
               savedFrame.valid ? 1 : 0);
      log.Log(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE", dbg);
    }

    if (savedFrame.valid) {
      for (int i = 1; i < 32; i++) {
        cpu.SetRegister(i, savedFrame.gpr[i]);
      }
      cpu.SetHI(savedFrame.hi);
      cpu.SetLO(savedFrame.lo);
      savedFrame.valid = false;
    }

    // RFE: pop the SR interrupt enable/kernel-user mode stack
    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(epc);
    break;
  }

  // B0:19h HookEntryInt — register the game's IRQ handler
  case 0x19: {
    hookedEntryIntHandler = cpu.GetRegister(4); // $a0 = handler address
    auto &log2 = AIO::Emulator::Common::Logger::Instance();
    char buf[128];
    snprintf(buf, sizeof(buf), "HookEntryInt: handler=0x%08X",
             hookedEntryIntHandler);
    log2.Log(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE", buf);
    break;
  }

  // B0:32h OpenEvent(class, spec, mode, func)
  case 0x32: {
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
    }
    // Descriptor format: 0xF1000000 | slot
    cpu.SetRegister(2, (slot >= 0) ? (0xF1000000u | static_cast<uint32_t>(slot))
                                   : 0xFFFFFFFF);
    break;
  }

  // B0:33h CloseEvent(descriptor)
  case 0x33: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot] = {};
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:34h WaitEvent(descriptor) — returns 1 when event fired, 0 otherwise
  case 0x34: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS && events[slot].fired) {
      events[slot].fired = false;
      cpu.SetRegister(2, 1);
    } else {
      cpu.SetRegister(2, 0);
    }
    break;
  }

  // B0:35h TestEvent(descriptor) — non-blocking check
  case 0x35: {
    int slot = cpu.GetRegister(4) & 0xFF;
    bool ready = (slot < MAX_EVENTS && events[slot].fired);
    if (ready)
      events[slot].fired = false;
    cpu.SetRegister(2, ready ? 1u : 0u);
    break;
  }

  // B0:36h EnableEvent(descriptor)
  case 0x36: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = true;
      events[slot].fired = false;
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:37h DisableEvent(descriptor)
  case 0x37: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = false;
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:12h InitPad — initialize pad buffers
  case 0x12:
    cpu.SetRegister(2, 1);
    break;

  // B0:13h StartPad — start pad communication
  case 0x13:
    break;

  // B0:14h StopPad
  case 0x14:
    break;

  // B0:15h PAD_init2
  case 0x15:
    cpu.SetRegister(2, 1);
    break;

  // B0:18h SetDefaultExitFromException
  case 0x18:
    cpu.SetRegister(2, 0);
    break;

  // B0:3Dh std_out_putchar — consume silently
  case 0x3D:
    break;

  // B0:3Fh printf — consume silently
  case 0x3F:
    break;

  // B0:47h AddDevice — no-op
  case 0x47:
    cpu.SetRegister(2, 1);
    break;

  // B0:4Ah-4Dh InitCard, StartCard, StopCard, _card_info_subfunc — no-op
  case 0x4A:
  case 0x4B:
  case 0x4C:
  case 0x4D:
    cpu.SetRegister(2, 1);
    break;

  // B0:56h GetC0Table — return address 0xC0 (not used meaningfully in HLE)
  case 0x56:
    cpu.SetRegister(2, 0xC0);
    break;

  // B0:57h GetB0Table — return address 0xB0
  case 0x57:
    cpu.SetRegister(2, 0xB0);
    break;

  // B0:5Bh ChangeClearPAD — initialize pad system, enable VBlank IRQ
  case 0x5B: {
    auto &irqs = ps1.GetInterrupts();
    // Enable VBlank in the interrupt controller mask
    uint32_t mask = irqs.ReadMask();
    mask |= IRQ::VBLANK;
    irqs.WriteMask(mask);

    // Enable interrupts in COP0 SR: set IEc (bit 0) and IM2 (bit 10) for hw IRQ
    // line
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
}

// ─── C-Table (0xC0) ─────────────────────────────────────────────────────

void PS1HleBios::DispatchC0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();

  switch (func) {

  // C0:00h EnqueueTimerAndVblankIrqs — no-op (handled by emulator hardware)
  case 0x00:
    break;

  // C0:01h EnqueueSyscallHandler — no-op
  case 0x01:
    break;

  // C0:02h SysEnqIntRP — no-op
  case 0x02:
    cpu.SetRegister(2, 0);
    break;

  // C0:03h SysDeqIntRP — no-op
  case 0x03:
    cpu.SetRegister(2, 0);
    break;

  // C0:07h InstallExceptionHandlers — no-op (HLE handles exceptions)
  case 0x07:
    break;

  // C0:08h SysInitMemory — no-op (memory already available)
  case 0x08:
    break;

  // C0:0Ah ChangeClearPad — initialize pad, enable VBlank IRQ
  case 0x0A: {
    auto &irqs = ps1.GetInterrupts();
    uint32_t mask = irqs.ReadMask();
    mask |= IRQ::VBLANK;
    irqs.WriteMask(mask);

    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr |= CPU::SR::IEc | (1u << 10);
    cpu.SetCOP0(CPU::COP0::SR, sr);
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

  // C0:1Ch SetConf — no-op
  case 0x1C:
    break;

  default:
    cpu.SetRegister(2, 0);
    break;
  }
}

// ─── Event Delivery ─────────────────────────────────────────────────────

void PS1HleBios::DeliverEvent(uint32_t classId, uint32_t spec) {
  for (auto &ev : events) {
    if (ev.used && ev.enabled && ev.classId == classId && ev.spec == spec) {
      ev.fired = true;
    }
  }
}

// ─── BIOS Region ────────────────────────────────────────────────────────

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
  // The PS1 kernel exception handler lives at 0x80000080.
  // Games trigger SYSCALL → CPU jumps to 0x80000080.
  // The real BIOS dispatches based on r9 (function number) for A/B/C calls.
  //
  // For HLE, we install a minimal handler that just returns:
  //   MFC0 $k0, $14    (load EPC into k0)
  //   ADDIU $k0, $k0, 4  (skip past the SYSCALL instruction)
  //   JR $k0
  //   RFE               (restore interrupt state in delay slot)
  //
  // This allows games to call SYSCALL without hanging — the calls
  // become no-ops. Many games only need printf/putchar (which we can
  // safely ignore) and memory init (handled by direct EXE loading).

  uint32_t excBase = 0x80; // offset in RAM for 0x80000080

  // MFC0 $k0, EPC
  WriteRAMInstr(memory, excBase, MFC0(26, CPU::COP0::EPC));
  // ADDIU $k0, $k0, 4 — skip the SYSCALL instruction
  WriteRAMInstr(memory, excBase + 4, ADDIU(26, 26, 4));
  // JR $k0
  WriteRAMInstr(memory, excBase + 8, 0x03400008);
  // RFE (delay slot)
  WriteRAMInstr(memory, excBase + 12, RFE());

  // Kernel call vectors at 0xA0, 0xB0, 0xC0 (A/B/C function tables)
  // The real BIOS puts jump targets here. Games call these addresses.
  // We stub them to just return immediately.
  for (uint32_t tableAddr : {0xA0u, 0xB0u, 0xC0u}) {
    // Install a JR $ra + NOP at the virtual addresses
    // But these are called via function pointers, not exception vectors.
    // Actually games jump to 0x000000A0 etc which is in RAM.
    WriteRAMInstr(memory, tableAddr, JR_RA());
    WriteRAMInstr(memory, tableAddr + 4, NOP());
  }
}

// ─── GPU Initialization ─────────────────────────────────────────────────

void PS1HleBios::InitGPU(PS1GPU &gpu) { gpu.Reset(); }

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

  // Step 6: Zero-fill BSS if specified
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

} // namespace AIO::Emulator::PS1
