#pragma once

#include "PS1Constants.h"
#include <cstdint>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class PS1;
class PS1Memory;
class R3000A;
class CDROM;
class PS1GPU;

// PS-X EXE header (2048 bytes, first 0x800 of the executable file on disc)
struct PSXExeHeader {
  char magic[8];          // "PS-X EXE"
  uint32_t pad1[2];
  uint32_t initialPC;     // Entry point
  uint32_t initialGP;     // Initial GP ($28)
  uint32_t destAddr;      // RAM destination address
  uint32_t fileSize;      // Size of .text section (data to load)
  uint32_t pad2[2];
  uint32_t memfillStart;  // BSS start (memfill)
  uint32_t memfillSize;   // BSS size
  uint32_t initialSP;     // Initial SP ($29) base
  uint32_t spOffset;      // SP offset (added to base)
};

// High-Level Emulation BIOS for PS1
// Allows running games without a real BIOS dump by emulating the
// kernel boot sequence and critical A/B/C table SYSCALL functions.
class PS1HleBios {
public:
  // Attempt to boot the loaded disc via HLE.
  // Parses the PS-X EXE from the disc image, loads it into RAM,
  // initializes hardware registers, installs the exception handler,
  // and sets CPU PC to the game's entry point.
  // Returns true on success.
  static bool InitHLE(PS1 &ps1);

private:
  // Parse the PS-X EXE from a raw BIN/ISO disc image
  static bool FindAndLoadExe(PS1Memory &memory, CDROM &cdrom,
                             R3000A &cpu, PS1GPU &gpu);

  // Install the HLE exception handler at EXCEPTION_VECTOR (0x80000080)
  // and kernel function tables in low RAM
  static void InstallKernelStubs(PS1Memory &memory);

  // Populate the BIOS region with a minimal bootstrap that jumps
  // to the HLE entry or provides a NOP sled for exception vectors
  static void PopulateBiosRegion(PS1Memory &memory);

  // Initialize GPU to a sane default display mode
  static void InitGPU(PS1GPU &gpu);

  // Write a MIPS instruction into the BIOS region
  static void WriteBIOSInstr(PS1Memory &memory, uint32_t physOffset,
                             uint32_t instr);

  // Write a MIPS instruction into RAM
  static void WriteRAMInstr(PS1Memory &memory, uint32_t offset,
                            uint32_t instr);

  // MIPS instruction encoding helpers
  static constexpr uint32_t NOP() { return 0x00000000; }
  static constexpr uint32_t JR_RA() { return 0x03E00008; }
  static constexpr uint32_t ADDIU(uint32_t rt, uint32_t rs, int16_t imm) {
    return (0x09 << 26) | (rs << 21) | (rt << 16) | (static_cast<uint16_t>(imm));
  }
  static constexpr uint32_t LUI(uint32_t rt, uint16_t imm) {
    return (0x0F << 26) | (rt << 16) | imm;
  }
  static constexpr uint32_t ORI(uint32_t rt, uint32_t rs, uint16_t imm) {
    return (0x0D << 26) | (rs << 21) | (rt << 16) | imm;
  }
  static constexpr uint32_t J(uint32_t target26) {
    return (0x02 << 26) | (target26 & 0x03FFFFFF);
  }
  static constexpr uint32_t MFC0(uint32_t rt, uint32_t rd) {
    return (0x10 << 26) | (0x00 << 21) | (rt << 16) | (rd << 11);
  }
  static constexpr uint32_t MTC0(uint32_t rt, uint32_t rd) {
    return (0x10 << 26) | (0x04 << 21) | (rt << 16) | (rd << 11);
  }
  static constexpr uint32_t RFE() {
    return (0x10 << 26) | (1 << 25) | 0x10;
  }
};

} // namespace AIO::Emulator::PS1
