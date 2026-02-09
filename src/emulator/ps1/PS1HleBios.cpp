#include "emulator/ps1/PS1HleBios.h"
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

  PopulateBiosRegion(memory);
  InstallKernelStubs(memory);
  InitGPU(gpu);

  if (!FindAndLoadExe(memory, cdrom, cpu, gpu)) {
    return false;
  }

  return true;
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

void PS1HleBios::InitGPU(PS1GPU &gpu) {
  gpu.Reset();
}

// ─── EXE Loading from Disc Image ───────────────────────────────────────

bool PS1HleBios::FindAndLoadExe(PS1Memory &memory, CDROM &cdrom,
                                R3000A &cpu, PS1GPU &gpu) {
  const auto *discData = cdrom.GetDiscDataPointer();
  size_t discSize = cdrom.GetDiscDataSize();

  if (!discData || discSize == 0) {
    return false;
  }

  // PS1 disc layout (BIN/CUE raw image):
  // Each sector = 2352 bytes
  // Sector data starts at byte 24 (after 12-byte sync + 4-byte header)
  // The system area is sectors 0-15
  // The PS-X EXE is typically found via the filesystem starting at sector 16+
  //
  // Simplified approach: scan for "PS-X EXE" magic in the disc image.
  // This handles both raw BIN images and various layouts.

  const char magic[] = "PS-X EXE";
  const uint8_t *exeLocation = nullptr;

  // First try the standard location: SYSTEM.CNF points to the EXE,
  // but it's usually at a well-known sector. Most PS1 BIN images have
  // the EXE starting after the filesystem area. Scan for the magic.
  for (size_t i = 0; i + sizeof(PSXExeHeader) < discSize; i++) {
    if (std::memcmp(discData + i, magic, 8) == 0) {
      exeLocation = discData + i;
      break;
    }
  }

  if (!exeLocation) {
    return false;
  }

  PSXExeHeader header;
  std::memcpy(&header, exeLocation, sizeof(header));

  // Validate header
  if (header.destAddr == 0 || header.fileSize == 0) {
    return false;
  }

  // The EXE data follows the 2048-byte header
  const uint8_t *exeData = exeLocation + 0x800;
  size_t remainingDisc = discSize - (exeData - discData);

  if (header.fileSize > remainingDisc) {
    return false;
  }

  // Load the EXE text section into RAM at destAddr
  uint32_t destPhys = header.destAddr & 0x1FFFFF; // Mask to 2MB RAM
  uint8_t *ramPtr = memory.GetRAMPointer();

  if (destPhys + header.fileSize <= MemSize::RAM) {
    std::memcpy(ramPtr + destPhys, exeData, header.fileSize);
  } else {
    // Load what fits
    size_t loadable = MemSize::RAM - destPhys;
    std::memcpy(ramPtr + destPhys, exeData, loadable);
  }

  // Zero-fill BSS if specified
  if (header.memfillSize > 0 && header.memfillStart != 0) {
    uint32_t bssPhys = header.memfillStart & 0x1FFFFF;
    uint32_t bssEnd = bssPhys + header.memfillSize;
    if (bssEnd > MemSize::RAM)
      bssEnd = MemSize::RAM;
    if (bssPhys < bssEnd) {
      std::memset(ramPtr + bssPhys, 0, bssEnd - bssPhys);
    }
  }

  // Set CPU initial state
  cpu.SetPC(header.initialPC);

  if (header.initialGP != 0) {
    cpu.SetRegister(28, header.initialGP); // $gp
  }

  uint32_t sp = header.initialSP;
  if (sp == 0)
    sp = 0x801FFF00; // Default stack top
  sp += header.spOffset;
  cpu.SetRegister(29, sp); // $sp
  cpu.SetRegister(30, sp); // $fp = $sp

  // Enable COP0 status: interrupts disabled, kernel mode, BEV=0
  // so exceptions go to 0x80000080 (our HLE handler)
  cpu.SetCOP0(CPU::COP0::SR, CPU::SR::CU0 | CPU::SR::CU2);

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
