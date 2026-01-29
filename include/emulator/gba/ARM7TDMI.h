#pragma once
#include "ARM7TDMIConstants.h"
#include "ARM7TDMIHelpers.h"
#include <cstdint>
#include <vector>

namespace AIO::Emulator::GBA {
using namespace ARM7TDMIConstants;
using namespace ARM7TDMIHelpers;

// Crash notification callback (set by GUI)
extern void (*CrashPopupCallback)(const char *logPath);

class GBAMemory;

class ARM7TDMI {
public:
  explicit ARM7TDMI(GBAMemory &memory);
  ~ARM7TDMI();

  void Reset();
  void Step();

  // HLE timing: some BIOS SWIs (and other high-level emulation helpers)
  // advance peripheral time without executing individual CPU instructions.
  // We accumulate those cycles here so the outer emulation loop can account
  // for them in its cycle budget.
  int ConsumeHLECycles();
  // Poll interrupts explicitly (for synchronizing after peripherals run)
  void PollInterrupts();
  void StepBack();
  // Debugger API
  void AddBreakpoint(uint32_t addr);
  void RemoveBreakpoint(uint32_t addr);
  void ClearBreakpoints();
  const std::vector<uint32_t> &GetBreakpoints() const;
  void SetSingleStep(bool enabled);
  bool IsSingleStep() const;
  // Halt state
  void Continue();
  void DumpState(std::ostream &os) const;

  // Test Helpers
  uint32_t GetRegister(int index) const { return registers[index]; }
  void SetRegister(int index, uint32_t value) { registers[index] = value; }
  uint32_t GetCPSR() const { return cpsr; }
  void SetCPSR(uint32_t value) { cpsr = value; }
  void SetThumbMode(bool thumb) {
    thumbMode = thumb;
    SetCPSRFlag(cpsr, CPSR::FLAG_T, thumb);
  }
  bool IsHalted() const { return halted; }
  bool IsThumbModeFlag() const { return thumbMode; }
  struct CpuSnapshot {
    uint32_t registers[16];
    uint32_t cpsr;
    uint32_t spsr;
    bool thumbMode;
    std::vector<uint8_t> iwram; // 32KB snapshot of IWRAM
  };
  std::vector<CpuSnapshot> cpuHistory;

private:
  GBAMemory &memory;

  // Current instruction context (captured in Step())
  uint32_t currentInstrAddr{0};
  bool currentInstrThumb{false};
  uint16_t currentOp16{0};
  uint32_t currentOp32{0};

  // Registers
  // R0-R12: General Purpose
  // R13: SP (Stack Pointer)
  // R14: LR (Link Register)
  // R15: PC (Program Counter)
  uint32_t registers[16];

  // CPSR: Current Program Status Register
  // CPSR: Saved Program Status Register (banked)
  uint32_t cpsr;
  uint32_t spsr;
  bool thumbMode = false;
  bool halted = false;
  // Halt reason tracking:
  // - sleepHalt: HALT/STOP/IntrWait-style halts which should resume on IRQ
  // - debuggerHalt: debugger-triggered halts (breakpoints/stepback) which must
  // NOT auto-resume
  bool sleepHalt = false;
  bool debuggerHalt = false;
  // Debugger
  std::vector<uint32_t> breakpoints;
  bool singleStep{false};

  // Pipeline
  // ARM7TDMI has a 3-stage pipeline: Fetch, Decode, Execute
  // We might simulate this or just do direct execution for simplicity initially

  void Fetch();
  void Decode(uint32_t instruction);
  void DecodeThumb(uint16_t instruction, uint32_t pcValue);
  void Execute();

  // Instruction Implementations
  void ExecuteBranch(uint32_t instruction);
  void ExecuteBX(uint32_t instruction);
  void ExecuteDataProcessing(uint32_t instruction);
  void ExecuteMultiply(uint32_t instruction);
  void ExecuteMultiplyLong(uint32_t instruction);
  void ExecuteSingleDataTransfer(uint32_t instruction);
  void ExecuteHalfwordDataTransfer(uint32_t instruction);
  void ExecuteBlockDataTransfer(uint32_t instruction);
  void ExecuteBIOSFunction(uint32_t biosPC);
  void ExecuteSWI(uint32_t comment);

  void AdvanceHLECycles(int cycles);

  int hleCyclesThisStep = 0;
  void ExecuteMRS(uint32_t instruction);
  void ExecuteMSR(uint32_t instruction);

  int logInstructions = 0;

  // Helpers
  void SetZN(uint32_t result);
  bool CheckCondition(uint32_t cond);
  void CheckInterrupts();
  void TriggerUndefinedException(uint32_t instruction);
  void SwitchMode(uint32_t newMode);

  long long instructionCount = 0;

  // Banked Registers storage
  uint32_t r13_svc, r14_svc, spsr_svc;
  uint32_t r13_irq, r14_irq, spsr_irq;
  uint32_t r13_und, r14_und, spsr_und;
  uint32_t r13_usr, r14_usr; // User/System share these
  // We can ignore FIQ/Abort/Undef for now as they are rarely used in standard
  // GBA games

  struct IrqContext {
    uint32_t r0, r1, r2, r3, r12, lr, pc, cpsr;
    bool thumbMode;
  };
  std::vector<IrqContext> irqStack;
  static const uint32_t MAGIC_IRQ_RETURN = 0xFFFFFF00;

  uint16_t irqPendingClear = 0;
};

} // namespace AIO::Emulator::GBA
