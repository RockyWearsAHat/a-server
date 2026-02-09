#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class PS1Memory;
class GTE;

// Pending load slot for load delay emulation
struct PendingLoad {
  uint32_t reg = 0;
  uint32_t value = 0;
  bool active = false;
};

class R3000A : public Common::Loggable {
public:
  explicit R3000A(PS1Memory &memory);
  ~R3000A() = default;

  void Reset();
  int Step(); // Execute one instruction, returns cycles consumed

  // ─── GTE (COP2) ─────────────────────────────────────────────────────
  void SetGTE(GTE *gte) { this->gte = gte; }

  // ─── Register Access ────────────────────────────────────────────────
  uint32_t GetRegister(uint32_t index) const;
  void SetRegister(uint32_t index, uint32_t value);
  uint32_t GetPC() const { return pc; }
  void SetPC(uint32_t value) { pc = value; }
  uint32_t GetHI() const { return hi; }
  uint32_t GetLO() const { return lo; }
  void SetHI(uint32_t value) { hi = value; }
  void SetLO(uint32_t value) { lo = value; }

  // ─── COP0 ───────────────────────────────────────────────────────────
  uint32_t GetCOP0(uint32_t index) const;
  void SetCOP0(uint32_t index, uint32_t value);
  uint32_t GetSR() const { return cop0[CPU::COP0::SR]; }
  uint32_t GetCause() const { return cop0[CPU::COP0::CAUSE]; }
  uint32_t GetEPC() const { return cop0[CPU::COP0::EPC]; }

  // ─── Exception Handling ─────────────────────────────────────────────
  void TriggerException(uint32_t excCode);
  void TriggerInterrupt();
  bool IsInterruptPending() const;

  // ─── Pipeline / Delay Slot State ────────────────────────────────────
  bool InDelaySlot() const { return inDelaySlot; }

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;
  uint32_t GetCurrentInstruction() const { return currentInstruction; }

  // Total instructions executed (for deterministic tooling)
  uint64_t GetInstructionCount() const { return instructionCount; }
  uint64_t GetCycleCount() const { return cycleCount; }

private:
  PS1Memory &memory;
  GTE *gte = nullptr;

  // ─── Registers ──────────────────────────────────────────────────────
  std::array<uint32_t, 32> regs{};
  uint32_t pc = CPU::RESET_VECTOR;
  uint32_t nextPc = CPU::RESET_VECTOR + 4;
  uint32_t hi = 0;
  uint32_t lo = 0;
  std::array<uint32_t, 16> cop0{};

  // ─── Pipeline State ─────────────────────────────────────────────────
  bool inDelaySlot = false;
  bool branchPending = false;
  uint32_t branchTarget = 0;
  uint32_t currentInstruction = 0;

  // Load delay slot
  PendingLoad pendingLoad{};
  PendingLoad nextLoad{};

  // ─── Statistics ─────────────────────────────────────────────────────
  uint64_t instructionCount = 0;
  uint64_t cycleCount = 0;

  // ─── Instruction Decoding & Execution ───────────────────────────────
  void ExecuteInstruction(uint32_t instr);

  // R-type (opcode 0x00)
  void ExecuteSpecial(uint32_t instr);
  void OpSLL(uint32_t instr);
  void OpSRL(uint32_t instr);
  void OpSRA(uint32_t instr);
  void OpSLLV(uint32_t instr);
  void OpSRLV(uint32_t instr);
  void OpSRAV(uint32_t instr);
  void OpJR(uint32_t instr);
  void OpJALR(uint32_t instr);
  void OpSYSCALL(uint32_t instr);
  void OpBREAK(uint32_t instr);
  void OpMFHI(uint32_t instr);
  void OpMTHI(uint32_t instr);
  void OpMFLO(uint32_t instr);
  void OpMTLO(uint32_t instr);
  void OpMULT(uint32_t instr);
  void OpMULTU(uint32_t instr);
  void OpDIV(uint32_t instr);
  void OpDIVU(uint32_t instr);
  void OpADD(uint32_t instr);
  void OpADDU(uint32_t instr);
  void OpSUB(uint32_t instr);
  void OpSUBU(uint32_t instr);
  void OpAND(uint32_t instr);
  void OpOR(uint32_t instr);
  void OpXOR(uint32_t instr);
  void OpNOR(uint32_t instr);
  void OpSLT(uint32_t instr);
  void OpSLTU(uint32_t instr);

  // I-type
  void OpBcondZ(uint32_t instr);
  void OpJ(uint32_t instr);
  void OpJAL(uint32_t instr);
  void OpBEQ(uint32_t instr);
  void OpBNE(uint32_t instr);
  void OpBLEZ(uint32_t instr);
  void OpBGTZ(uint32_t instr);
  void OpADDI(uint32_t instr);
  void OpADDIU(uint32_t instr);
  void OpSLTI(uint32_t instr);
  void OpSLTIU(uint32_t instr);
  void OpANDI(uint32_t instr);
  void OpORI(uint32_t instr);
  void OpXORI(uint32_t instr);
  void OpLUI(uint32_t instr);

  // Load/Store
  void OpLB(uint32_t instr);
  void OpLH(uint32_t instr);
  void OpLWL(uint32_t instr);
  void OpLW(uint32_t instr);
  void OpLBU(uint32_t instr);
  void OpLHU(uint32_t instr);
  void OpLWR(uint32_t instr);
  void OpSB(uint32_t instr);
  void OpSH(uint32_t instr);
  void OpSWL(uint32_t instr);
  void OpSW(uint32_t instr);
  void OpSWR(uint32_t instr);

  // Coprocessor
  void ExecuteCOP0(uint32_t instr);
  void OpMFC0(uint32_t instr);
  void OpMTC0(uint32_t instr);
  void OpRFE(uint32_t instr);

  void ExecuteCOP2(uint32_t instr); // GTE
  void OpLWC2(uint32_t instr);
  void OpSWC2(uint32_t instr);

  // ─── Helpers ────────────────────────────────────────────────────────
  static uint32_t GetOpcode(uint32_t instr) { return instr >> 26; }
  static uint32_t GetRS(uint32_t instr) { return (instr >> 21) & 0x1F; }
  static uint32_t GetRT(uint32_t instr) { return (instr >> 16) & 0x1F; }
  static uint32_t GetRD(uint32_t instr) { return (instr >> 11) & 0x1F; }
  static uint32_t GetShamt(uint32_t instr) { return (instr >> 6) & 0x1F; }
  static uint32_t GetFunct(uint32_t instr) { return instr & 0x3F; }
  static uint32_t GetImm16(uint32_t instr) { return instr & 0xFFFF; }
  static int32_t GetImm16SE(uint32_t instr) {
    return static_cast<int16_t>(instr & 0xFFFF);
  }
  static uint32_t GetTarget(uint32_t instr) { return instr & 0x3FFFFFF; }

  // Write to a register, enforcing R0 = 0 invariant
  void WriteReg(uint32_t index, uint32_t value);

  // Apply pending load delay
  void ApplyPendingLoad();

  // Branch helper
  void DoBranch(uint32_t target);
};

} // namespace AIO::Emulator::PS1
