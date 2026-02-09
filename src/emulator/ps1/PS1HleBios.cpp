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
  InstallTrampolines(memory);
  InitKernelState(memory);
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
  changeClearRCntFlags = {};
  b0TableRamAddr = 0;
  c0TableRamAddr = 0;
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

  // A0:00h open(filename, accessmode) — same as B0:32h
  case 0x00: {
    uint32_t nameAddr = cpu.GetRegister(4);
    uint32_t accessMode = cpu.GetRegister(5);

    auto &log = AIO::Emulator::Common::Logger::Instance();

    if (nameAddr < 0x1000 && (nameAddr & 0x1FFFFFFF) < 0x1000) {
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1.HLE",
                 "A0:open() spurious: nameAddr=0x%08X → fd=0", nameAddr);
      cpu.SetRegister(2, 0);
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
    } else if (filename[0] == '\0') {
      cpu.SetRegister(2, 0);
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

  // A0:2Ch memmove(dst, src, len) — same as memcpy for our purposes
  case 0x2C: {
    uint32_t dst = cpu.GetRegister(4);
    uint32_t src = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
    cpu.SetRegister(2, dst);
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

  // A0:27h bcopy(src, dst, len)
  case 0x27: {
    uint32_t src = cpu.GetRegister(4);
    uint32_t dst = cpu.GetRegister(5);
    uint32_t len = cpu.GetRegister(6);
    for (uint32_t i = 0; i < len; i++) {
      mem.Write8(dst + i, mem.Read8(src + i));
    }
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

  // A0:3Ch putchar(char) — silently consume
  case 0x3C:
    break;

  // A0:3Dh gets(dst) — no TTY input, return empty string
  case 0x3D: {
    uint32_t dst = cpu.GetRegister(4);
    mem.Write8(dst, 0);
    cpu.SetRegister(2, dst);
    break;
  }

  // A0:3Eh puts(src) — silently consume
  case 0x3E:
    break;

  // A0:3Fh printf — silently consume
  case 0x3F:
    break;

  // A0:44h FlushCache — no-op for HLE
  case 0x44:
    break;

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
// Per PSX-SPX: https://psx-spx.consoledev.net/kernelbios/#bios-function-summary
// GPU functions do NOT exist in the B-table; they are A-table only (A0:47-4B).

void PS1HleBios::DispatchB0(PS1 &ps1, uint8_t func) {
  auto &cpu = ps1.GetCPU();
  auto &mem = ps1.GetMemory();

  switch (func) {

  // B0:00h alloc_kernel_memory(size)
  case 0x00:
    cpu.SetRegister(2, 0);
    break;

  // B0:01h free_kernel_memory(buf)
  case 0x01:
    break;

  // B0:02h init_timer(t,reload,flags)
  case 0x02:
    cpu.SetRegister(2, 1);
    break;

  // B0:03h get_timer(t)
  case 0x03:
    cpu.SetRegister(2, 0);
    break;

  // B0:04h enable_timer_irq(t)
  case 0x04:
    cpu.SetRegister(2, 1);
    break;

  // B0:05h disable_timer_irq(t)
  case 0x05:
    cpu.SetRegister(2, 1);
    break;

  // B0:06h restart_timer(t)
  case 0x06:
    cpu.SetRegister(2, 1);
    break;

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
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:0Ah WaitEvent(event)
  case 0x0A: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS && events[slot].fired) {
      events[slot].fired = false;
      cpu.SetRegister(2, 1);
    } else {
      cpu.SetRegister(2, 0);
    }
    break;
  }

  // B0:0Bh TestEvent(event)
  case 0x0B: {
    int slot = cpu.GetRegister(4) & 0xFF;
    bool ready = (slot < MAX_EVENTS && events[slot].fired);
    if (ready)
      events[slot].fired = false;
    cpu.SetRegister(2, ready ? 1u : 0u);
    break;
  }

  // B0:0Ch EnableEvent(event)
  case 0x0C: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = true;
      events[slot].fired = false;
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:0Dh DisableEvent(event)
  case 0x0D: {
    int slot = cpu.GetRegister(4) & 0xFF;
    if (slot < MAX_EVENTS) {
      events[slot].enabled = false;
    }
    cpu.SetRegister(2, 1);
    break;
  }

  // B0:12h InitPAD2(buf1,siz1,buf2,siz2)
  case 0x12:
    cpu.SetRegister(2, 1);
    break;

  // B0:13h StartPAD2
  case 0x13:
    break;

  // B0:14h StopPAD2
  case 0x14:
    break;

  // B0:15h PAD_init2(type, button_dest, unused, unused)
  case 0x15:
    cpu.SetRegister(2, 2);
    break;

  // B0:16h PAD_dr
  case 0x16:
    cpu.SetRegister(2, 0xFFFFFFFF);
    break;

  // B0:17h ReturnFromException
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

    uint32_t sr = cpu.GetCOP0(CPU::COP0::SR);
    sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
    cpu.SetCOP0(CPU::COP0::SR, sr);
    cpu.SetPC(epc);
    break;
  }

  // B0:18h ResetEntryInt
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
    for (auto &ev : events) {
      if (ev.used && ev.enabled && ev.fired && ev.classId == classId &&
          ev.spec == spec && ev.mode == 0x2000) {
        ev.fired = false;
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
          "open() spurious call: nameAddr=0x%08X mode=0x%X ra=0x%08X → fd=0",
          nameAddr, accessMode, cpu.GetRegister(31));
      cpu.SetRegister(2, 0);
      break;
    }

    // Mask to physical for reading the filename string
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
    } else if (filename[0] == '\0') {
      // Empty filename — probably spurious call, return stdin
      cpu.SetRegister(2, 0);
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

  // B0:3Fh puts(src) — consume silently
  case 0x3F:
    break;

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
  // Garbage area at 0x00000000 — must be non-zero for games like R-Types
  // that read from address 0 via uninitialized pointers.
  // Real BIOS copies the exception handler here; we write a recognizable
  // pattern that satisfies the non-zero requirement.
  WriteRAMInstr(memory, 0x00, 0x00000003); // real BIOS overwrites [0] with 3
  WriteRAMInstr(memory, 0x04, 0x275A0C80); // part of exc handler copy
  WriteRAMInstr(memory, 0x08, 0x03400008); // jr k0
  WriteRAMInstr(memory, 0x0C, 0x00000000); // nop

  // RAM size in megabytes at [0x60]
  memory.WriteRAM32(0x60, 2);
  // Unknown kernel vars at fixed offsets
  memory.WriteRAM32(0x64, 0x00000000);
  memory.WriteRAM32(0x68, 0x000000FF);

  // Exception vector at 0x80000080:
  // The real BIOS installs 4 opcodes that jump to the kernel exception handler.
  // For HLE, the CPU intercepts PC=0x80 before fetching, so these are only
  // read by games that patch the exception handler (e.g., Metal Gear Solid).
  // We install the standard pattern that games expect to find:
  //   lui  k0, upper(handler)
  //   addiu k0, lower(handler)
  //   jr   k0
  //   nop
  // Point to our BIOS region handler stub at 0xBFC00180 (physical 0x1FC00180)
  uint32_t excBase = 0x80;
  WriteRAMInstr(memory, excBase, LUI(26, 0xA000));         // lui k0, 0xA000
  WriteRAMInstr(memory, excBase + 4, ORI(26, 26, 0x0080)); // ori k0, k0, 0x0080
  WriteRAMInstr(memory, excBase + 8, 0x03400008);          // jr k0
  WriteRAMInstr(memory, excBase + 12, NOP());              // nop (delay slot)

  // A/B/C call vectors at 0xA0, 0xB0, 0xC0 in RAM
  // Games call these via JR; the CPU intercepts at these addresses for HLE.
  // Install JR $ra + NOP as fallback in case the intercept is bypassed.
  for (uint32_t tableAddr : {0xA0u, 0xB0u, 0xC0u}) {
    WriteRAMInstr(memory, tableAddr, JR_RA());
    WriteRAMInstr(memory, tableAddr + 4, NOP());
    // The real BIOS has 4 opcodes per vector (16 bytes)
    WriteRAMInstr(memory, tableAddr + 8, NOP());
    WriteRAMInstr(memory, tableAddr + 12, NOP());
  }

  // Install a return stub in kernel memory that games can call
  // (used to fill jump table entries)
  WriteRAMInstr(memory, STUB_RET_ADDR, JR_RA());
  WriteRAMInstr(memory, STUB_RET_ADDR + 4, NOP());
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

  // C(06h) = ExceptionHandler — games read this to patch exception handling
  // Point to the code we installed at 0x80000080
  memory.WriteRAM32(C0_TABLE_ADDR + 0x06 * 4, 0x80000080);

  // ─── Zero-fill all control block regions ──────────────────────────
  uint8_t *ram = memory.GetRAMPointer();
  std::memset(ram + EXCB_ADDR, 0, 0x20);
  std::memset(ram + PCB_ADDR, 0, 0x04);
  std::memset(ram + TCB_ADDR, 0, 0x300);
  std::memset(ram + EVCB_ADDR, 0, 0x1C0);
  std::memset(ram + FCB_ADDR, 0, 0x2C0);
  std::memset(ram + DCB_ADDR, 0, 0x320);

  // ─── Table of Tables at 0x100-0x157 ───────────────────────────────
  // Each entry: [base_addr, total_size] as KSEG0 addresses
  memory.WriteRAM32(0x100, 0x80000000 | EXCB_ADDR); // ExCB base
  memory.WriteRAM32(0x104, 4 * 0x08);               // ExCB size
  memory.WriteRAM32(0x108, 0x80000000 | PCB_ADDR);  // PCB base
  memory.WriteRAM32(0x10C, 1 * 0x04);               // PCB size
  memory.WriteRAM32(0x110, 0x80000000 | TCB_ADDR);  // TCB base
  memory.WriteRAM32(0x114, 4 * 0xC0);               // TCB size
  memory.WriteRAM32(0x118, 0);                      // unused
  memory.WriteRAM32(0x11C, 0);                      // unused
  memory.WriteRAM32(0x120, 0x80000000 | EVCB_ADDR); // EvCB base
  memory.WriteRAM32(0x124, 16 * 0x1C);              // EvCB size
  memory.WriteRAM32(0x128, 0);                      // unused
  memory.WriteRAM32(0x12C, 0);                      // unused
  memory.WriteRAM32(0x130, 0);                      // unused
  memory.WriteRAM32(0x134, 0);                      // unused
  memory.WriteRAM32(0x138, 0);                      // unused
  memory.WriteRAM32(0x13C, 0);                      // unused
  memory.WriteRAM32(0x140, 0x80000000 | FCB_ADDR);  // FCB base
  memory.WriteRAM32(0x144, 16 * 0x2C);              // FCB size
  memory.WriteRAM32(0x148, 0);                      // unused
  memory.WriteRAM32(0x14C, 0);                      // unused
  memory.WriteRAM32(0x150, 0x80000000 | DCB_ADDR);  // DCB base
  memory.WriteRAM32(0x154, 10 * 0x50);              // DCB size

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
