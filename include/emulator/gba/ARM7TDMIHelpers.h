#pragma once

#include "ARM7TDMIConstants.h"
#include <cstdint>

/**
 * ARM7TDMI CPU Helper Functions - Encapsulate common bit operations
 * Derived from ARM Architecture Reference Manual
 */
namespace AIO::Emulator::GBA::ARM7TDMIHelpers {

using namespace ARM7TDMIConstants;

// ===== CPSR FLAG OPERATIONS =====

/**
 * Set or clear a single CPSR flag
 */
inline void SetCPSRFlag(uint32_t &cpsr, uint32_t flag, bool value) {
  if (value) {
    cpsr |= flag;
  } else {
    cpsr &= ~flag;
  }
}

/**
 * Read a single CPSR flag
 */
inline bool GetCPSRFlag(uint32_t cpsr, uint32_t flag) {
  return (cpsr & flag) != 0;
}

/**
 * Extract and return a CPSR flag value (0 or 1)
 */
inline uint32_t GetCPSRFlagValue(uint32_t cpsr, uint32_t flag) {
  return GetCPSRFlag(cpsr, flag) ? 1 : 0;
}

/**
 * Check if carry flag is set in CPSR
 */
inline bool CarryFlagSet(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_C);
}

/**
 * Check if zero flag is set in CPSR
 */
inline bool ZeroFlagSet(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_Z);
}

/**
 * Check if negative flag is set in CPSR
 */
inline bool NegativeFlagSet(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_N);
}

/**
 * Check if overflow flag is set in CPSR
 */
inline bool OverflowFlagSet(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_V);
}

/**
 * Check if IRQ is disabled
 */
inline bool IRQDisabled(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_I);
}

/**
 * Check if FIQ is disabled
 */
inline bool FIQDisabled(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_F);
}

/**
 * Check if CPU is in Thumb mode
 */
inline bool IsThumbMode(uint32_t cpsr) {
  return GetCPSRFlag(cpsr, CPSR::FLAG_T);
}

/**
 * Get current CPU mode from CPSR
 */
inline uint32_t GetCPUMode(uint32_t cpsr) { return cpsr & CPSR::MODE_MASK; }

/**
 * Set CPU mode in CPSR
 */
inline void SetCPUMode(uint32_t &cpsr, uint32_t mode) {
  cpsr = (cpsr & ~CPSR::MODE_MASK) | (mode & CPSR::MODE_MASK);
}

// ===== CONDITION CODE EVALUATION =====

/**
 * Check if condition code is satisfied based on current CPSR flags
 * Returns true if instruction should execute, false otherwise
 */
inline bool ConditionSatisfied(uint32_t condition, uint32_t cpsr) {
  bool n = NegativeFlagSet(cpsr);
  bool z = ZeroFlagSet(cpsr);
  bool c = CarryFlagSet(cpsr);
  bool v = OverflowFlagSet(cpsr);

  switch (condition) {
  case Condition::EQ:
    return z; // Z set
  case Condition::NE:
    return !z; // Z clear
  case Condition::CS:
    return c; // C set (CS/HS)
  case Condition::CC:
    return !c; // C clear (CC/LO)
  case Condition::MI:
    return n; // N set
  case Condition::PL:
    return !n; // N clear
  case Condition::VS:
    return v; // V set
  case Condition::VC:
    return !v; // V clear
  case Condition::HI:
    return c && !z; // C set and Z clear
  case Condition::LS:
    return !c || z; // C clear or Z set
  case Condition::GE:
    return n == v; // N == V
  case Condition::LT:
    return n != v; // N != V
  case Condition::GT:
    return !z && (n == v); // Z clear and N == V
  case Condition::LE:
    return z || (n != v); // Z set or N != V
  case Condition::AL:
    return true; // Always
  case Condition::NV:
    return false; // Never (reserved)
  default:
    return false;
  }
}

// ===== BIT EXTRACTION HELPERS =====

/**
 * Extract bits from value at specified position and mask
 * @param value The value to extract from
 * @param shift The bit position to start extracting from (LSB = 0)
 * @param mask The mask to apply after shifting
 * @return The extracted bits
 */
inline uint32_t ExtractBits(uint32_t value, uint32_t shift, uint32_t mask) {
  return (value >> shift) & mask;
}

/**
 * Extract register index field from ARM instruction [3:0]
 */
inline uint32_t ExtractRegisterField(uint32_t instruction, uint32_t shift) {
  return (instruction >> shift) & 0xF;
}

/**
 * Extract 3-bit field (for Thumb register indices)
 */
inline uint32_t Extract3BitField(uint32_t instruction, uint32_t shift) {
  return (instruction >> shift) & 0x7;
}

/**
 * Extract 5-bit field (for shift amounts)
 */
inline uint32_t Extract5BitField(uint32_t instruction, uint32_t shift) {
  return (instruction >> shift) & 0x1F;
}

/**
 * Extract 8-bit immediate field from Thumb instruction
 */
inline uint32_t Extract8BitImmediate(uint16_t instruction) {
  return instruction & 0xFF;
}

/**
 * Extract 24-bit signed offset from ARM branch instruction
 */
inline int32_t ExtractBranchOffset(uint32_t instruction) {
  int32_t offset = instruction & ARMInstructionFormat::B_OFFSET_MASK;
  // Sign extend from 24 bits
  if (offset & 0x00800000) {
    offset |= 0xFF000000;
  }
  return offset;
}

// ===== SHIFT OPERATIONS WITH CARRY =====

/**
 * Perform logical shift left, optionally updating carry flag
 * @param value The value to shift
 * @param amount The number of positions to shift
 * @param cpsr Current CPSR (updated if updateCarry is true)
 * @param updateCarry Whether to update the carry flag
 * @return The shifted value
 */
inline uint32_t LogicalShiftLeft(uint32_t value, uint32_t amount,
                                 uint32_t &cpsr, bool updateCarry = false) {
  // ARM7TDMI semantics (register shifts):
  // - amount==0: result=value, carry unchanged
  // - 1..31: carry = bit(32-amount)
  // - 32: result=0, carry = bit0
  // - >32: result=0, carry = 0
  if (amount == 0)
    return value;

  if (updateCarry) {
    if (amount < 32) {
      const bool carryOut = (value & (1U << (32 - amount))) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    } else if (amount == 32) {
      const bool carryOut = (value & 1U) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    } else {
      SetCPSRFlag(cpsr, CPSR::FLAG_C, false);
    }
  }

  if (amount >= 32)
    return 0;
  return value << amount;
}

/**
 * Perform logical shift right, optionally updating carry flag
 */
inline uint32_t LogicalShiftRight(uint32_t value, uint32_t amount,
                                  uint32_t &cpsr, bool updateCarry = false) {
  // ARM7TDMI semantics (register shifts):
  // - amount==0: result=value, carry unchanged
  // - 1..31: carry = bit(amount-1)
  // - 32: result=0, carry = bit31
  // - >32: result=0, carry = 0
  if (amount == 0)
    return value;

  if (updateCarry) {
    if (amount < 32) {
      const bool carryOut = (value & (1U << (amount - 1))) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    } else if (amount == 32) {
      const bool carryOut = (value & 0x80000000U) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    } else {
      SetCPSRFlag(cpsr, CPSR::FLAG_C, false);
    }
  }

  if (amount >= 32)
    return 0;
  return value >> amount;
}

/**
 * Perform arithmetic shift right, optionally updating carry flag
 */
inline uint32_t ArithmeticShiftRight(uint32_t value, uint32_t amount,
                                     uint32_t &cpsr, bool updateCarry = false) {
  // ARM7TDMI semantics (register shifts):
  // - amount==0: result=value, carry unchanged
  // - 1..31: carry = bit(amount-1)
  // - >=32: result fills with sign bit; carry = bit31
  if (amount == 0)
    return value;

  if (updateCarry) {
    if (amount < 32) {
      const bool carryOut = (((int32_t)value >> (amount - 1)) & 1) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    } else {
      const bool carryOut = (value & 0x80000000U) != 0;
      SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
    }
  }

  if (amount >= 32) {
    return ((int32_t)value < 0) ? 0xFFFFFFFFU : 0U;
  }
  return (uint32_t)((int32_t)value >> amount);
}

/**
 * Perform rotate right, optionally updating carry flag
 */
inline uint32_t RotateRight(uint32_t value, uint32_t amount, uint32_t &cpsr,
                            bool updateCarry = false) {
  amount &= 0x1F; // Only use lower 5 bits
  if (amount == 0)
    return value;

  if (updateCarry) {
    bool carryOut = (value & (1U << (amount - 1))) != 0;
    SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
  }

  return (value >> amount) | (value << (32 - amount));
}

/**
 * Perform rotate right extended (uses carry flag as LSB)
 */
inline uint32_t RotateRightExtended(uint32_t value, uint32_t &cpsr) {
  uint32_t carryIn = CarryFlagSet(cpsr) ? 1 : 0;
  bool carryOut = (value & 1) != 0;
  uint32_t result = (value >> 1) | (carryIn << 31);
  SetCPSRFlag(cpsr, CPSR::FLAG_C, carryOut);
  return result;
}

// ===== BARREL SHIFTER (ARM-style shift with register or immediate) =====

/**
 * Apply barrel shifter logic (used in data processing instructions)
 * @param value The value to shift
 * @param shiftType Type of shift (LSL, LSR, ASR, ROR)
 * @param shiftAmount Amount to shift by
 * @param cpsr Current CPSR (may be updated with carry)
 * @param updateCarry Whether to update carry flag
 * @return The shifted value
 */
inline uint32_t BarrelShift(uint32_t value, uint32_t shiftType,
                            uint32_t shiftAmount, uint32_t &cpsr,
                            bool updateCarry = false) {
  // ROR with shiftAmount=0 encodes RRX, so don't early-return for that case
  if (shiftAmount == 0 && shiftType != Shift::RRX && shiftType != Shift::ROR) {
    return value;
  }

  switch (shiftType) {
  case Shift::LSL:
    return LogicalShiftLeft(value, shiftAmount, cpsr, updateCarry);
  case Shift::LSR:
    return LogicalShiftRight(value, shiftAmount, cpsr, updateCarry);
  case Shift::ASR:
    return ArithmeticShiftRight(value, shiftAmount, cpsr, updateCarry);
  case Shift::ROR:
    if (shiftAmount == 0) {
      // ROR #0 is actually RRX (rotate right extended)
      return RotateRightExtended(value, cpsr);
    }
    // Register-specified ROR: if amount != 0 and (amount & 31) == 0, the value
    // is unchanged but carry-out becomes bit31 when flags are updated.
    if ((shiftAmount & 0x1F) == 0) {
      if (updateCarry) {
        SetCPSRFlag(cpsr, CPSR::FLAG_C, (value & 0x80000000U) != 0);
      }
      return value;
    }
    return RotateRight(value, shiftAmount, cpsr, updateCarry);
  case Shift::RRX:
    return RotateRightExtended(value, cpsr);
  default:
    return value;
  }
}

// ===== ARITHMETIC HELPERS =====

/**
 * Update NZ flags based on result value
 */
inline void UpdateNZFlags(uint32_t &cpsr, uint32_t result) {
  SetCPSRFlag(cpsr, CPSR::FLAG_Z, result == 0);
  SetCPSRFlag(cpsr, CPSR::FLAG_N, (result & 0x80000000) != 0);
}

/**
 * Detect signed overflow for addition
 * @return true if signed overflow occurred
 */
inline bool DetectAddOverflow(uint32_t a, uint32_t b, uint32_t result) {
  // Overflow occurs if:
  // - Both operands have same sign
  // - Result has different sign from operands
  int32_t sa = (int32_t)a;
  int32_t sb = (int32_t)b;
  int32_t sr = (int32_t)result;
  return ((sa ^ sr) & (sb ^ sr)) & 0x80000000;
}

/**
 * Detect signed overflow for subtraction
 * @return true if signed overflow occurred
 */
inline bool DetectSubOverflow(uint32_t a, uint32_t b, uint32_t result) {
  // Overflow occurs if:
  // - Operands have different signs
  // - Result has different sign from first operand
  int32_t sa = (int32_t)a;
  int32_t sb = (int32_t)b;
  int32_t sr = (int32_t)result;
  return ((sa ^ sb) & (sa ^ sr)) & 0x80000000;
}

/**
 * Detect carry from addition (unsigned overflow)
 */
inline bool DetectAddCarry(uint32_t a, uint32_t b) {
  return (a + b) < a; // Simpler: result wrapped around
}

/**
 * Detect borrow from subtraction (unsigned underflow)
 */
inline bool DetectSubBorrow(uint32_t a, uint32_t b) { return a < b; }

} // namespace AIO::Emulator::GBA::ARM7TDMIHelpers
