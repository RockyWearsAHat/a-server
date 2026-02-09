#include "emulator/ps1/R3000A.h"
#include "emulator/ps1/GTE.h"
#include "emulator/ps1/PS1.h"
#include "emulator/ps1/PS1HleBios.h"
#include "emulator/ps1/PS1Memory.h"
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

R3000A::R3000A(PS1Memory &memory) : Loggable("PS1.CPU"), memory(memory) {}

void R3000A::Reset() {
  regs.fill(0);
  pc = CPU::RESET_VECTOR;
  nextPc = CPU::RESET_VECTOR + 4;
  hi = 0;
  lo = 0;
  cop0.fill(0);
  cop0[CPU::COP0::PRID] = 0x00000002; // R3000A revision

  inDelaySlot = false;
  branchPending = false;
  branchTarget = 0;
  currentInstruction = 0;
  pendingLoad = {};
  nextLoad = {};

  instructionCount = 0;
  cycleCount = 0;
}

void R3000A::WriteReg(uint32_t index, uint32_t value) {
  regs[index] = value;
  regs[0] = 0; // R0 is hardwired to zero
}

void R3000A::ApplyPendingLoad() {
  if (pendingLoad.active) {
    WriteReg(pendingLoad.reg, pendingLoad.value);
    pendingLoad.active = false;
  }
}

void R3000A::DoBranch(uint32_t target) {
  // Set the branch target directly — it takes effect after the delay slot
  // At this point pc = delay slot address, nextPc = delay slot + 4
  // Overwriting nextPc means the step AFTER the delay slot fetches from target
  branchPending = true;
  nextPc = target;
}

bool R3000A::TryHLETrap() {
  if (!ps1)
    return false;

  uint32_t physPc = pc & 0x1FFFFFFF;

  // HLE exception handler at 0x80
  if (physPc == 0x80) {
    PS1HleBios::HandleException(*ps1);
    return true;
  }

  // BIOS call vectors
  if (physPc != 0xA0 && physPc != 0xB0 && physPc != 0xC0)
    return false;

  uint8_t tableId = static_cast<uint8_t>(physPc);
  uint8_t funcNum = static_cast<uint8_t>(regs[9]); // $t1 holds function number

  PS1HleBios::Dispatch(*ps1, tableId, funcNum);

  // Return to caller
  pc = regs[31]; // $ra
  nextPc = pc + 4;
  return true;
}

int R3000A::Step() {
  // HLE BIOS vector intercept — handle before fetching the stub instruction
  if (TryHLETrap()) {
    instructionCount++;
    cycleCount++;
    return 1;
  }

  currentInstruction = memory.Read32(pc);

  if constexpr (Trace::CPU) {
    LogDebug("PC=%08X INSTR=%08X", pc, currentInstruction);
  }

  // Temporary: periodic PC sampling for debugging (every ~4M instructions)
  if ((instructionCount & 0x3FFFFF) == 0) {
    LogInfo("PC=%08X instr=%llu", pc, (unsigned long long)instructionCount);
  }

  // One-shot diagnostic: log when PC lands in low memory (not BIOS trampoline)
  {
    static bool crashLogged = false;
    if (!crashLogged && pc < 0x80000000 && pc != 0xA0 && pc != 0xB0 &&
        pc != 0xC0 && pc != 0x80) {
      LogInfo("CRASH-DETECT: PC=0x%08X instr=%llu r31(RA)=0x%08X r2(v0)=0x%08X "
              "r4(a0)=0x%08X r29(sp)=0x%08X",
              pc, (unsigned long long)instructionCount, regs[31], regs[2],
              regs[4], regs[29]);
      crashLogged = true;
    }
  }

  uint32_t currentPc = pc;
  pc = nextPc;
  nextPc += 4;

  // Track delay slot for exception handling (EPC needs to point at branch)
  bool wasInDelaySlot = inDelaySlot;
  inDelaySlot = branchPending;
  branchPending = false;

  // Load delay pipeline: MIPS I has a 1-slot load delay.
  // Instruction N executes LW → sets nextLoad.
  // At N+1: promote nextLoad → pendingLoad, execute (can't see the value),
  // then ApplyPendingLoad writes the register. N+2 sees it.
  pendingLoad = nextLoad;
  nextLoad = {};

  ExecuteInstruction(currentInstruction);

  ApplyPendingLoad();

  instructionCount++;
  cycleCount++;
  return 1; // Each instruction takes 1 cycle (simplified)
}

// ─── Instruction Dispatch ──────────────────────────────────────────────

void R3000A::ExecuteInstruction(uint32_t instr) {
  uint32_t opcode = GetOpcode(instr);

  switch (opcode) {
  case 0x00:
    ExecuteSpecial(instr);
    break;
  case 0x01:
    OpBcondZ(instr);
    break;
  case 0x02:
    OpJ(instr);
    break;
  case 0x03:
    OpJAL(instr);
    break;
  case 0x04:
    OpBEQ(instr);
    break;
  case 0x05:
    OpBNE(instr);
    break;
  case 0x06:
    OpBLEZ(instr);
    break;
  case 0x07:
    OpBGTZ(instr);
    break;
  case 0x08:
    OpADDI(instr);
    break;
  case 0x09:
    OpADDIU(instr);
    break;
  case 0x0A:
    OpSLTI(instr);
    break;
  case 0x0B:
    OpSLTIU(instr);
    break;
  case 0x0C:
    OpANDI(instr);
    break;
  case 0x0D:
    OpORI(instr);
    break;
  case 0x0E:
    OpXORI(instr);
    break;
  case 0x0F:
    OpLUI(instr);
    break;
  case 0x10:
    ExecuteCOP0(instr);
    break;
  case 0x12:
    ExecuteCOP2(instr);
    break;
  case 0x20:
    OpLB(instr);
    break;
  case 0x21:
    OpLH(instr);
    break;
  case 0x22:
    OpLWL(instr);
    break;
  case 0x23:
    OpLW(instr);
    break;
  case 0x24:
    OpLBU(instr);
    break;
  case 0x25:
    OpLHU(instr);
    break;
  case 0x26:
    OpLWR(instr);
    break;
  case 0x28:
    OpSB(instr);
    break;
  case 0x29:
    OpSH(instr);
    break;
  case 0x2A:
    OpSWL(instr);
    break;
  case 0x2B:
    OpSW(instr);
    break;
  case 0x2E:
    OpSWR(instr);
    break;
  case 0x32:
    OpLWC2(instr);
    break;
  case 0x3A:
    OpSWC2(instr);
    break;
  default:
    TriggerException(CPU::ExcCode::RESERVED_INSTR);
    break;
  }
}

void R3000A::ExecuteSpecial(uint32_t instr) {
  switch (GetFunct(instr)) {
  case 0x00:
    OpSLL(instr);
    break;
  case 0x02:
    OpSRL(instr);
    break;
  case 0x03:
    OpSRA(instr);
    break;
  case 0x04:
    OpSLLV(instr);
    break;
  case 0x06:
    OpSRLV(instr);
    break;
  case 0x07:
    OpSRAV(instr);
    break;
  case 0x08:
    OpJR(instr);
    break;
  case 0x09:
    OpJALR(instr);
    break;
  case 0x0C:
    OpSYSCALL(instr);
    break;
  case 0x0D:
    OpBREAK(instr);
    break;
  case 0x10:
    OpMFHI(instr);
    break;
  case 0x11:
    OpMTHI(instr);
    break;
  case 0x12:
    OpMFLO(instr);
    break;
  case 0x13:
    OpMTLO(instr);
    break;
  case 0x18:
    OpMULT(instr);
    break;
  case 0x19:
    OpMULTU(instr);
    break;
  case 0x1A:
    OpDIV(instr);
    break;
  case 0x1B:
    OpDIVU(instr);
    break;
  case 0x20:
    OpADD(instr);
    break;
  case 0x21:
    OpADDU(instr);
    break;
  case 0x22:
    OpSUB(instr);
    break;
  case 0x23:
    OpSUBU(instr);
    break;
  case 0x24:
    OpAND(instr);
    break;
  case 0x25:
    OpOR(instr);
    break;
  case 0x26:
    OpXOR(instr);
    break;
  case 0x27:
    OpNOR(instr);
    break;
  case 0x2A:
    OpSLT(instr);
    break;
  case 0x2B:
    OpSLTU(instr);
    break;
  default:
    TriggerException(CPU::ExcCode::RESERVED_INSTR);
    break;
  }
}

// ─── ALU R-type ────────────────────────────────────────────────────────

void R3000A::OpSLL(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRT(instr)] << GetShamt(instr));
}
void R3000A::OpSRL(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRT(instr)] >> GetShamt(instr));
}
void R3000A::OpSRA(uint32_t instr) {
  WriteReg(GetRD(instr),
           static_cast<uint32_t>(static_cast<int32_t>(regs[GetRT(instr)]) >>
                                 GetShamt(instr)));
}
void R3000A::OpSLLV(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRT(instr)] << (regs[GetRS(instr)] & 0x1F));
}
void R3000A::OpSRLV(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRT(instr)] >> (regs[GetRS(instr)] & 0x1F));
}
void R3000A::OpSRAV(uint32_t instr) {
  WriteReg(GetRD(instr),
           static_cast<uint32_t>(static_cast<int32_t>(regs[GetRT(instr)]) >>
                                 (regs[GetRS(instr)] & 0x1F)));
}

void R3000A::OpADD(uint32_t instr) {
  int32_t a = static_cast<int32_t>(regs[GetRS(instr)]);
  int32_t b = static_cast<int32_t>(regs[GetRT(instr)]);
  int64_t result = static_cast<int64_t>(a) + b;
  if (result > INT32_MAX || result < INT32_MIN) {
    TriggerException(CPU::ExcCode::ARITHMETIC_OVERFLOW);
    return;
  }
  WriteReg(GetRD(instr), static_cast<uint32_t>(result));
}

void R3000A::OpADDU(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] + regs[GetRT(instr)]);
}

void R3000A::OpSUB(uint32_t instr) {
  int32_t a = static_cast<int32_t>(regs[GetRS(instr)]);
  int32_t b = static_cast<int32_t>(regs[GetRT(instr)]);
  int64_t result = static_cast<int64_t>(a) - b;
  if (result > INT32_MAX || result < INT32_MIN) {
    TriggerException(CPU::ExcCode::ARITHMETIC_OVERFLOW);
    return;
  }
  WriteReg(GetRD(instr), static_cast<uint32_t>(result));
}

void R3000A::OpSUBU(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] - regs[GetRT(instr)]);
}
void R3000A::OpAND(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] & regs[GetRT(instr)]);
}
void R3000A::OpOR(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] | regs[GetRT(instr)]);
}
void R3000A::OpXOR(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] ^ regs[GetRT(instr)]);
}
void R3000A::OpNOR(uint32_t instr) {
  WriteReg(GetRD(instr), ~(regs[GetRS(instr)] | regs[GetRT(instr)]));
}
void R3000A::OpSLT(uint32_t instr) {
  WriteReg(GetRD(instr), static_cast<int32_t>(regs[GetRS(instr)]) <
                                 static_cast<int32_t>(regs[GetRT(instr)])
                             ? 1
                             : 0);
}
void R3000A::OpSLTU(uint32_t instr) {
  WriteReg(GetRD(instr), regs[GetRS(instr)] < regs[GetRT(instr)] ? 1 : 0);
}

void R3000A::OpMFHI(uint32_t instr) { WriteReg(GetRD(instr), hi); }
void R3000A::OpMTHI(uint32_t instr) { hi = regs[GetRS(instr)]; }
void R3000A::OpMFLO(uint32_t instr) { WriteReg(GetRD(instr), lo); }
void R3000A::OpMTLO(uint32_t instr) { lo = regs[GetRS(instr)]; }

void R3000A::OpMULT(uint32_t instr) {
  int64_t result =
      static_cast<int64_t>(static_cast<int32_t>(regs[GetRS(instr)])) *
      static_cast<int64_t>(static_cast<int32_t>(regs[GetRT(instr)]));
  lo = static_cast<uint32_t>(result);
  hi = static_cast<uint32_t>(result >> 32);
}

void R3000A::OpMULTU(uint32_t instr) {
  uint64_t result = static_cast<uint64_t>(regs[GetRS(instr)]) *
                    static_cast<uint64_t>(regs[GetRT(instr)]);
  lo = static_cast<uint32_t>(result);
  hi = static_cast<uint32_t>(result >> 32);
}

void R3000A::OpDIV(uint32_t instr) {
  int32_t n = static_cast<int32_t>(regs[GetRS(instr)]);
  int32_t d = static_cast<int32_t>(regs[GetRT(instr)]);
  if (d == 0) {
    hi = static_cast<uint32_t>(n);
    lo = (n >= 0) ? 0xFFFFFFFF : 1;
  } else if (n == INT32_MIN && d == -1) {
    hi = 0;
    lo = static_cast<uint32_t>(INT32_MIN);
  } else {
    lo = static_cast<uint32_t>(n / d);
    hi = static_cast<uint32_t>(n % d);
  }
}

void R3000A::OpDIVU(uint32_t instr) {
  uint32_t n = regs[GetRS(instr)];
  uint32_t d = regs[GetRT(instr)];
  if (d == 0) {
    hi = n;
    lo = 0xFFFFFFFF;
  } else {
    lo = n / d;
    hi = n % d;
  }
}

// ─── Branch/Jump ───────────────────────────────────────────────────────

void R3000A::OpJ(uint32_t instr) {
  DoBranch((pc & 0xF0000000) | (GetTarget(instr) << 2));
}

void R3000A::OpJAL(uint32_t instr) {
  WriteReg(CPU::REG_RA, nextPc);
  DoBranch((pc & 0xF0000000) | (GetTarget(instr) << 2));
}

void R3000A::OpJR(uint32_t instr) { DoBranch(regs[GetRS(instr)]); }

void R3000A::OpJALR(uint32_t instr) {
  WriteReg(GetRD(instr), nextPc);
  DoBranch(regs[GetRS(instr)]);
}

void R3000A::OpBEQ(uint32_t instr) {
  if (regs[GetRS(instr)] == regs[GetRT(instr)])
    DoBranch(pc + (GetImm16SE(instr) << 2));
}

void R3000A::OpBNE(uint32_t instr) {
  if (regs[GetRS(instr)] != regs[GetRT(instr)])
    DoBranch(pc + (GetImm16SE(instr) << 2));
}

void R3000A::OpBLEZ(uint32_t instr) {
  if (static_cast<int32_t>(regs[GetRS(instr)]) <= 0)
    DoBranch(pc + (GetImm16SE(instr) << 2));
}

void R3000A::OpBGTZ(uint32_t instr) {
  if (static_cast<int32_t>(regs[GetRS(instr)]) > 0)
    DoBranch(pc + (GetImm16SE(instr) << 2));
}

void R3000A::OpBcondZ(uint32_t instr) {
  bool negative = static_cast<int32_t>(regs[GetRS(instr)]) < 0;
  bool link = (GetRT(instr) & 0x1E) == 0x10;
  bool bgez = (GetRT(instr) & 1) != 0;

  bool branch = negative ^ bgez;
  if (link)
    WriteReg(CPU::REG_RA, nextPc);
  if (branch)
    DoBranch(pc + (GetImm16SE(instr) << 2));
}

// ─── ALU Immediate ────────────────────────────────────────────────────

void R3000A::OpADDI(uint32_t instr) {
  int32_t a = static_cast<int32_t>(regs[GetRS(instr)]);
  int32_t imm = GetImm16SE(instr);
  int64_t result = static_cast<int64_t>(a) + imm;
  if (result > INT32_MAX || result < INT32_MIN) {
    TriggerException(CPU::ExcCode::ARITHMETIC_OVERFLOW);
    return;
  }
  WriteReg(GetRT(instr), static_cast<uint32_t>(result));
}

void R3000A::OpADDIU(uint32_t instr) {
  WriteReg(GetRT(instr),
           regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr)));
}
void R3000A::OpSLTI(uint32_t instr) {
  WriteReg(GetRT(instr),
           static_cast<int32_t>(regs[GetRS(instr)]) < GetImm16SE(instr) ? 1
                                                                        : 0);
}
void R3000A::OpSLTIU(uint32_t instr) {
  WriteReg(GetRT(instr),
           regs[GetRS(instr)] < static_cast<uint32_t>(GetImm16SE(instr)) ? 1
                                                                         : 0);
}
void R3000A::OpANDI(uint32_t instr) {
  WriteReg(GetRT(instr), regs[GetRS(instr)] & GetImm16(instr));
}
void R3000A::OpORI(uint32_t instr) {
  WriteReg(GetRT(instr), regs[GetRS(instr)] | GetImm16(instr));
}
void R3000A::OpXORI(uint32_t instr) {
  WriteReg(GetRT(instr), regs[GetRS(instr)] ^ GetImm16(instr));
}
void R3000A::OpLUI(uint32_t instr) {
  WriteReg(GetRT(instr), GetImm16(instr) << 16);
}

// ─── Load Instructions (with load delay slot) ──────────────────────────

void R3000A::OpLB(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  int8_t val = static_cast<int8_t>(memory.Read8(addr));
  nextLoad = {GetRT(instr), static_cast<uint32_t>(static_cast<int32_t>(val)),
              true};
}

void R3000A::OpLBU(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  nextLoad = {GetRT(instr), memory.Read8(addr), true};
}

void R3000A::OpLH(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 1) {
    TriggerException(CPU::ExcCode::ADDR_LOAD);
    return;
  }
  int16_t val = static_cast<int16_t>(memory.Read16(addr));
  nextLoad = {GetRT(instr), static_cast<uint32_t>(static_cast<int32_t>(val)),
              true};
}

void R3000A::OpLHU(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 1) {
    TriggerException(CPU::ExcCode::ADDR_LOAD);
    return;
  }
  nextLoad = {GetRT(instr), memory.Read16(addr), true};
}

void R3000A::OpLW(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 3) {
    TriggerException(CPU::ExcCode::ADDR_LOAD);
    return;
  }
  nextLoad = {GetRT(instr), memory.Read32(addr), true};
}

void R3000A::OpLWL(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  uint32_t aligned = memory.Read32(addr & ~3u);
  uint32_t rt = regs[GetRT(instr)];
  // If there's a pending load targeting the same register, use that value
  if (pendingLoad.active && pendingLoad.reg == GetRT(instr))
    rt = pendingLoad.value;

  switch (addr & 3) {
  case 0:
    rt = (rt & 0x00FFFFFF) | (aligned << 24);
    break;
  case 1:
    rt = (rt & 0x0000FFFF) | (aligned << 16);
    break;
  case 2:
    rt = (rt & 0x000000FF) | (aligned << 8);
    break;
  case 3:
    rt = aligned;
    break;
  }
  nextLoad = {GetRT(instr), rt, true};
}

void R3000A::OpLWR(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  uint32_t aligned = memory.Read32(addr & ~3u);
  uint32_t rt = regs[GetRT(instr)];
  if (pendingLoad.active && pendingLoad.reg == GetRT(instr))
    rt = pendingLoad.value;

  switch (addr & 3) {
  case 0:
    rt = aligned;
    break;
  case 1:
    rt = (rt & 0xFF000000) | (aligned >> 8);
    break;
  case 2:
    rt = (rt & 0xFFFF0000) | (aligned >> 16);
    break;
  case 3:
    rt = (rt & 0xFFFFFF00) | (aligned >> 24);
    break;
  }
  nextLoad = {GetRT(instr), rt, true};
}

// ─── Store Instructions ────────────────────────────────────────────────

void R3000A::OpSB(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  memory.Write8(addr, static_cast<uint8_t>(regs[GetRT(instr)]));
}

void R3000A::OpSH(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 1) {
    TriggerException(CPU::ExcCode::ADDR_STORE);
    return;
  }
  memory.Write16(addr, static_cast<uint16_t>(regs[GetRT(instr)]));
}

void R3000A::OpSW(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 3) {
    TriggerException(CPU::ExcCode::ADDR_STORE);
    return;
  }
  memory.Write32(addr, regs[GetRT(instr)]);
}

void R3000A::OpSWL(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  uint32_t aligned = memory.Read32(addr & ~3u);
  uint32_t rt = regs[GetRT(instr)];

  switch (addr & 3) {
  case 0:
    aligned = (aligned & 0xFFFFFF00) | (rt >> 24);
    break;
  case 1:
    aligned = (aligned & 0xFFFF0000) | (rt >> 16);
    break;
  case 2:
    aligned = (aligned & 0xFF000000) | (rt >> 8);
    break;
  case 3:
    aligned = rt;
    break;
  }
  memory.Write32(addr & ~3u, aligned);
}

void R3000A::OpSWR(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  uint32_t aligned = memory.Read32(addr & ~3u);
  uint32_t rt = regs[GetRT(instr)];

  switch (addr & 3) {
  case 0:
    aligned = rt;
    break;
  case 1:
    aligned = (aligned & 0x000000FF) | (rt << 8);
    break;
  case 2:
    aligned = (aligned & 0x0000FFFF) | (rt << 16);
    break;
  case 3:
    aligned = (aligned & 0x00FFFFFF) | (rt << 24);
    break;
  }
  memory.Write32(addr & ~3u, aligned);
}

// ─── Syscall / Break ───────────────────────────────────────────────────

void R3000A::OpSYSCALL([[maybe_unused]] uint32_t instr) {
  TriggerException(CPU::ExcCode::SYSCALL);
}
void R3000A::OpBREAK([[maybe_unused]] uint32_t instr) {
  TriggerException(CPU::ExcCode::BREAKPOINT);
}

// ─── COP0 ──────────────────────────────────────────────────────────────

void R3000A::ExecuteCOP0(uint32_t instr) {
  uint32_t rs = GetRS(instr);
  switch (rs) {
  case 0x00:
    OpMFC0(instr);
    break;
  case 0x04:
    OpMTC0(instr);
    break;
  case 0x10:
    if ((instr & 0x3F) == 0x10)
      OpRFE(instr);
    break;
  default:
    TriggerException(CPU::ExcCode::COP_UNUSABLE);
    break;
  }
}

void R3000A::OpMFC0(uint32_t instr) {
  uint32_t rd = GetRD(instr);
  nextLoad = {GetRT(instr), cop0[rd], true};
}

void R3000A::OpMTC0(uint32_t instr) {
  uint32_t rd = GetRD(instr);
  uint32_t value = regs[GetRT(instr)];
  cop0[rd] = value;

  // Cache isolation toggled via SR.Isc
  if (rd == CPU::COP0::SR) {
    memory.SetCacheIsolated((value & CPU::SR::Isc) != 0);
  }
}

void R3000A::OpRFE([[maybe_unused]] uint32_t instr) {
  // Restore interrupt/kernel mode bits: shift the 2-deep stack
  uint32_t sr = cop0[CPU::COP0::SR];
  sr = (sr & ~0xF) | ((sr >> 2) & 0xF);
  cop0[CPU::COP0::SR] = sr;
}

// ─── COP2 (GTE) ───────────────────────────────────────────────────────

void R3000A::ExecuteCOP2(uint32_t instr) {
  if (!(cop0[CPU::COP0::SR] & CPU::SR::CU2)) {
    TriggerException(CPU::ExcCode::COP_UNUSABLE);
    return;
  }

  uint32_t rs = GetRS(instr);
  if (rs & 0x10) {
    if (gte)
      gte->Execute(instr & 0x1FFFFFF);
    return;
  }

  uint32_t rt = GetRT(instr);
  uint32_t rd = GetRD(instr);
  switch (rs) {
  case 0x00: // MFC2
    if (gte)
      nextLoad = {rt, gte->ReadData(rd), true};
    break;
  case 0x02: // CFC2
    if (gte)
      nextLoad = {rt, gte->ReadControl(rd), true};
    break;
  case 0x04: // MTC2
    if (gte)
      gte->WriteData(rd, regs[rt]);
    break;
  case 0x06: // CTC2
    if (gte)
      gte->WriteControl(rd, regs[rt]);
    break;
  }
}

void R3000A::OpLWC2(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 3) {
    TriggerException(CPU::ExcCode::ADDR_LOAD);
    return;
  }
  uint32_t value = memory.Read32(addr);
  if (gte)
    gte->WriteData(GetRT(instr), value);
}

void R3000A::OpSWC2(uint32_t instr) {
  uint32_t addr = regs[GetRS(instr)] + static_cast<uint32_t>(GetImm16SE(instr));
  if (addr & 3) {
    TriggerException(CPU::ExcCode::ADDR_STORE);
    return;
  }
  uint32_t value = gte ? gte->ReadData(GetRT(instr)) : 0;
  memory.Write32(addr, value);
}

// ─── Exception Handling ────────────────────────────────────────────────

void R3000A::TriggerException(uint32_t excCode) {
  // Save PC to EPC
  uint32_t handlerAddr;

  if (cop0[CPU::COP0::SR] & CPU::SR::BEV) {
    handlerAddr = CPU::BOOT_EXCEPTION_VEC;
  } else {
    handlerAddr = CPU::EXCEPTION_VECTOR;
  }

  // Set Cause register
  uint32_t cause = cop0[CPU::COP0::CAUSE];
  cause &= ~0x7C; // Clear ExcCode field
  cause |= (excCode << 2);
  if (inDelaySlot) {
    cause |= (1u << 31);           // BD bit
    cop0[CPU::COP0::EPC] = pc - 4; // Point to branch instruction
  } else {
    cause &= ~(1u << 31);
    cop0[CPU::COP0::EPC] = pc;
  }
  cop0[CPU::COP0::CAUSE] = cause;

  // Push interrupt/kernel-mode stack in SR
  uint32_t sr = cop0[CPU::COP0::SR];
  sr = (sr & ~0x3F) | ((sr & 0xF) << 2);
  cop0[CPU::COP0::SR] = sr;

  pc = handlerAddr;
  nextPc = handlerAddr + 4;
  branchPending = false;

  if constexpr (Trace::EXCEPTIONS) {
    LogDebug("Exception: code=%u EPC=%08X handler=%08X", excCode,
             cop0[CPU::COP0::EPC], handlerAddr);
  }
}

void R3000A::TriggerInterrupt() { TriggerException(CPU::ExcCode::INTERRUPT); }

bool R3000A::IsInterruptPending() const {
  uint32_t sr = cop0[CPU::COP0::SR];
  uint32_t cause = cop0[CPU::COP0::CAUSE];

  // Interrupts enabled globally (IEc bit)
  if (!(sr & CPU::SR::IEc))
    return false;

  // Check if any enabled interrupt is pending
  return (sr & cause & CPU::SR::IM_MASK) != 0;
}

// ─── Register Access ──────────────────────────────────────────────────

uint32_t R3000A::GetRegister(uint32_t index) const {
  return (index < 32) ? regs[index] : 0;
}

void R3000A::SetRegister(uint32_t index, uint32_t value) {
  if (index > 0 && index < 32)
    regs[index] = value;
}

uint32_t R3000A::GetCOP0(uint32_t index) const {
  return (index < 16) ? cop0[index] : 0;
}

void R3000A::SetCOP0(uint32_t index, uint32_t value) {
  if (index < 16)
    cop0[index] = value;
}

// ─── Debug ──────────────────────────────────────────────────────────────

void R3000A::DumpState(std::ostream &os) const {
  os << "=== R3000A CPU State ===" << std::endl;
  os << "PC: " << std::hex << std::setw(8) << std::setfill('0') << pc
     << std::endl;
  os << "HI: " << std::hex << hi << " LO: " << lo << std::endl;
  for (int i = 0; i < 32; i++) {
    os << "R" << std::dec << i << ": " << std::hex << std::setw(8) << regs[i];
    if ((i & 3) == 3)
      os << std::endl;
    else
      os << "  ";
  }
  os << "SR: " << std::hex << cop0[CPU::COP0::SR]
     << " Cause: " << cop0[CPU::COP0::CAUSE] << " EPC: " << cop0[CPU::COP0::EPC]
     << std::endl;
  os << "Instructions: " << std::dec << instructionCount
     << " Cycles: " << cycleCount << std::endl;
}

std::string R3000A::GetDebugSummary() const {
  std::ostringstream os;
  os << "CPU PC=" << std::hex << pc << " instr=" << instructionCount;
  return os.str();
}

} // namespace AIO::Emulator::PS1
