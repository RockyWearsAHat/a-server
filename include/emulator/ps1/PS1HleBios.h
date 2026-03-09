#pragma once

#include "PS1Constants.h"
#include <array>
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
  char magic[8]; // "PS-X EXE"
  uint32_t pad1[2];
  uint32_t initialPC; // Entry point
  uint32_t initialGP; // Initial GP ($28)
  uint32_t destAddr;  // RAM destination address
  uint32_t fileSize;  // Size of .text section (data to load)
  uint32_t pad2[2];
  uint32_t memfillStart; // BSS start (memfill)
  uint32_t memfillSize;  // BSS size
  uint32_t initialSP;    // Initial SP ($29) base
  uint32_t spOffset;     // SP offset (added to base)
};

// High-Level Emulation BIOS for PS1
// Allows running games without a real BIOS dump by emulating the
// kernel boot sequence and critical A/B/C table SYSCALL functions.
class PS1HleBios {
public:
  static bool InitHLE(PS1 &ps1);

  // Called from R3000A::Step() when PC hits 0xA0/0xB0/0xC0.
  // Returns true if HLE handled the call and caller should set PC=$ra.
  // Returns false if:
  //   - The function table was patched (PC redirected to patched target), or
  //   - The handler set PC itself (e.g. ReturnFromException sets PC=EPC)
  static bool Dispatch(PS1 &ps1, uint8_t table, uint8_t func);

  // Called from R3000A::Step() when PC hits 0x80 (exception vector)
  static void HandleException(PS1 &ps1);

  // Fire events matching classId/spec (called at VBlank, IRQ delivery, etc.)
  static void DeliverEvent(uint32_t classId, uint32_t spec);

  // Reset HLE state (events, etc.)
  static void ResetState();

  // Callback return trampoline — mode=0x1000 event callbacks return here.
  // TryHLETrap intercepts this address and dispatches next pending callback
  // or resumes exception handling.
  static constexpr uint32_t CALLBACK_RETURN_ADDR = 0xF220;

  // Called from R3000A when CPU returns to CALLBACK_RETURN_ADDR trampoline
  static void ResumeAfterCallback(PS1 &ps1);

private:
  static bool FindAndLoadExe(PS1Memory &memory, CDROM &cdrom, R3000A &cpu,
                             PS1GPU &gpu);

  static void InstallKernelStubs(PS1Memory &memory);

  // Initialize kernel RAM state: Table of Tables, FCBs, DCBs, TCBs,
  // PCB, A0/B0/C0 jump tables, device strings, and pre-open stdin/stdout
  static void InitKernelState(PS1Memory &memory);

  // Populate the BIOS region with a minimal bootstrap that jumps
  // to the HLE entry or provides a NOP sled for exception vectors
  static void PopulateBiosRegion(PS1Memory &memory);

  // ─── Kernel RAM Layout Constants ────────────────────────────────────
  // All addresses are physical offsets within the first 64KB of RAM.
  // Real BIOS uses 0xE000-0xFFFF for kernel memory allocations.

  // A0 jump table: 256 entries × 4 bytes at fixed address 0x200
  static constexpr uint32_t A0_TABLE_ADDR = 0x200;
  static constexpr uint32_t A0_TABLE_ENTRIES = 256;

  // B0/C0 jump tables in kernel memory region
  static constexpr uint32_t B0_TABLE_ADDR = 0xE000;
  static constexpr uint32_t B0_TABLE_ENTRIES = 256;
  static constexpr uint32_t C0_TABLE_ADDR = 0xE400;
  static constexpr uint32_t C0_TABLE_ENTRIES = 128;

  // Trampoline stubs — 3 MIPS instructions (12 bytes) per entry.
  // Games read function pointers from the B0/C0/A0 tables and call them
  // directly. Each trampoline loads $t1 with the function number and jumps
  // to the 0xA0/0xB0/0xC0 vector so TryHLETrap can intercept the call.
  static constexpr uint32_t A0_TRAMPOLINE_ADDR = 0x1000; // 256 × 12 = 0xC00
  static constexpr uint32_t B0_TRAMPOLINE_ADDR = 0x2000; // 256 × 12 = 0xC00
  static constexpr uint32_t C0_TRAMPOLINE_ADDR = 0x3000; // 128 × 12 = 0x600

  // Control block regions
  static constexpr uint32_t EXCB_ADDR = 0xE600; // 4 × 0x08 = 0x20
  static constexpr uint32_t PCB_ADDR = 0xE620;  // 1 × 0x04
  static constexpr uint32_t TCB_ADDR = 0xE624;  // 4 × 0xC0 = 0x300
  static constexpr uint32_t EVCB_ADDR = 0xE924; // 16 × 0x1C = 0x1C0
  static constexpr uint32_t FCB_ADDR = 0xEB00;  // 16 × 0x2C = 0x2C0
  static constexpr uint32_t DCB_ADDR = 0xEDC0;  // 10 × 0x50 = 0x320

  // Exception handler code block — games read C(06h) and patch code at
  // offsets like +28h, +70h, +80h from it.  Real BIOS places this at 0xC80.
  // Must be far enough from the A0/B0/C0 vectors at 0xA0-0xCF.
  static constexpr uint32_t EXC_HANDLER_ADDR = 0xC80;
  static constexpr uint32_t EXC_HANDLER_SIZE = 0x100; // 256 bytes

  // Device name strings placed after the control blocks
  static constexpr uint32_t DEV_STRINGS_ADDR = 0xF0E0;

  // Stub return trampoline — JR $ra; NOP — for filling jump tables
  static constexpr uint32_t STUB_RET_ADDR = 0xF200;

  // Halt stub — infinite loop for game return (J self; NOP)
  // Real BIOS has a shell loop here; we just spin.
  static constexpr uint32_t HALT_ADDR = 0xF210;

  // Kernel heap region — used by SysInitMemory/alloc_kernel_memory.
  // Placed after all fixed kernel structures. The real BIOS uses ~8KB.
  static constexpr uint32_t KERNEL_HEAP_ADDR = 0xF300;
  static constexpr uint32_t KERNEL_HEAP_SIZE = 0x2000; // 8KB

  // Install trampoline stubs for all A0/B0/C0 table entries
  static void InstallTrampolines(PS1Memory &memory);

  // Initialize GPU to a sane default display mode
  static void InitGPU(PS1GPU &gpu);

  // Initialize CDROM hardware registers to post-BIOS-boot state
  static void InitCDROM(CDROM &cdrom);

  // Write a MIPS instruction into the BIOS region
  static void WriteBIOSInstr(PS1Memory &memory, uint32_t physOffset,
                             uint32_t instr);

  // Write a MIPS instruction into RAM
  static void WriteRAMInstr(PS1Memory &memory, uint32_t offset, uint32_t instr);

  // MIPS instruction encoding helpers
  static constexpr uint32_t NOP() { return 0x00000000; }
  static constexpr uint32_t JR_RA() { return 0x03E00008; }
  static constexpr uint32_t ADDIU(uint32_t rt, uint32_t rs, int16_t imm) {
    return (0x09 << 26) | (rs << 21) | (rt << 16) |
           (static_cast<uint16_t>(imm));
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
  static constexpr uint32_t RFE() { return (0x10 << 26) | (1 << 25) | 0x10; }

  // ─── A/B/C Table Dispatch ───────────────────────────────────────────
  static void DispatchA0(PS1 &ps1, uint8_t func);
  // Returns true = normal (return to $ra), false = PC already set by handler
  static bool DispatchB0(PS1 &ps1, uint8_t func);
  static void DispatchC0(PS1 &ps1, uint8_t func);

  // ─── Event System ───────────────────────────────────────────────────
  struct Event {
    uint32_t classId;
    uint32_t spec;
    uint32_t mode;
    uint32_t func;
    bool used;
    bool enabled;
    bool fired;
  };

  static constexpr int MAX_EVENTS = 32;
  static inline std::array<Event, MAX_EVENTS> events{};
  static inline bool interruptsEnabled = true;
  static inline uint32_t hookedEntryIntHandler = 0;

  // Saved CPU state from exception entry — restored by ReturnFromException
  struct ExceptionFrame {
    uint32_t gpr[32];
    uint32_t hi;
    uint32_t lo;
    bool valid;
  };
  static inline ExceptionFrame savedFrame;

  // User heap state for malloc/free (A0:33h InitHeap)
  static inline uint32_t heapBase = 0;
  static inline uint32_t heapSize = 0;
  static inline uint32_t heapPtr = 0;

  // Kernel heap state (C0:08h SysInitMemory / B0:00h alloc_kernel_memory)
  static inline uint32_t kernelHeapBase = 0;
  static inline uint32_t kernelHeapSize = 0;
  static inline uint32_t kernelHeapPtr = 0;

  // Handler chain array — 4 priority levels, stored in kernel heap RAM.
  // Each priority has a linked list of handler entries.
  // ToT[0x100] points to this array.
  static constexpr int NUM_HANDLER_PRIORITIES = 4;
  static inline uint32_t handlersArrayAddr = 0;

  // Random number generator
  static inline uint32_t hleSeed = 0;

  // Pad buffer state for HLE pad polling (InitPAD/StartPAD)
  static inline uint32_t padBuf1Addr = 0;
  static inline uint32_t padBuf1Size = 0;
  static inline uint32_t padBuf2Addr = 0;
  static inline uint32_t padBuf2Size = 0;
  static inline bool padStarted = false;

  // ChangeClearRCnt flags per timer (0..2) and vblank (3)
  static inline std::array<uint32_t, 4> changeClearRCntFlags{};

  // Whether the BIOS CDROM state-machine handlers are registered.
  // When true, the HLE processes CDROM IRQs inline (deliver events, ack).
  // When false, CDROM IRQs pass through to the game's longjmp handler.
  static inline bool cdromHandlersRegistered = false;

  // RAM addresses of B0/C0 jump tables (returned by GetB0Table/GetC0Table)
  static inline uint32_t b0TableRamAddr = 0;
  static inline uint32_t c0TableRamAddr = 0;

  // Nested exception guard: prevents TCB corruption when a second IRQ fires
  // while the longjmp/handler chain handler is still executing. The real PSX
  // kernel doesn't support nested exceptions — re-entrant IRQs are deferred
  // until ReturnFromException re-enables interrupts via RFE.
  static inline bool inExceptionHandler = false;

  // TCB pointer and CPU state captured at HandleException entry, before
  // callbacks can corrupt kernel data via decompressor mirror writes.
  static inline uint32_t savedTcbPtr = 0;
  static inline uint32_t savedRegs[32] = {};
  static inline uint32_t savedEpc = 0;
  static inline uint32_t savedHI = 0;
  static inline uint32_t savedLO = 0;
  static inline uint32_t savedSR = 0;

  // Cached memory pointer for DeliverEvent and event RAM sync
  static inline PS1Memory *memoryPtr = nullptr;

  // Mode=0x1000 callback execution queue. On real hardware, DeliverEvent
  // calls callbacks inline as subroutines. In HLE, we redirect the CPU to
  // execute each callback, returning via the CALLBACK_RETURN trampoline.
  static inline std::vector<uint32_t> pendingCallbacks;
  static inline PS1 *callbackPS1 = nullptr;

  // Dispatch the next pending callback or resume exception flow
  static void DispatchNextCallbackOrResume(PS1 &ps1);

  // EvCB RAM sync helpers
  static void WriteEvCBToRAM(int slot);
  static void ReadEvCBFromRAM(int slot);
  static constexpr uint32_t EvCBStatusFree = 0x0000;
  static constexpr uint32_t EvCBStatusDisabled = 0x1000;
  static constexpr uint32_t EvCBStatusBusy = 0x2000;
  static constexpr uint32_t EvCBStatusReady = 0x4000;
};

} // namespace AIO::Emulator::PS1
