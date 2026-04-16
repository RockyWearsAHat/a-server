#include "emulator/gb/LR35902.h"
#include "emulator/gb/GBMemory.h"

namespace GBEmulator {

LR35902::LR35902(GBMemory* memory)
    : pc_(0x0000),
      a_(0), b_(0), c_(0), d_(0), e_(0), h_(0), l_(0),
      f_(0), sp_(0xFFFF), halt_mode_(false),
      memory_(memory) {}

void LR35902::Reset() {
  pc_ = Read16(0xFFFC);  // Load from reset vector (or default to 0x0100 for GB)
  pc_ = 0x0100;          // Game Boy always starts at 0x0100
  sp_ = 0xFFFF;
  halt_mode_ = false;
}

uint32_t LR35902::Step() {
  if (halt_mode_) {
    // TODO: Check for interrupts and unhalt
    return 4;
  }

  uint8_t opcode = Read8(pc_);
  pc_++;

  return Execute(opcode);
}

LR35902::State LR35902::SaveState() const {
  return State{
    .pc = pc_,
    .a = a_, .b = b_, .c = c_, .d = d_, .e = e_, .h = h_, .l = l_,
    .f = f_,
    .sp = sp_,
    .halt_mode = halt_mode_,
  };
}

void LR35902::LoadState(const State& state) {
  pc_ = state.pc;
  a_ = state.a;
  b_ = state.b;
  c_ = state.c;
  d_ = state.d;
  e_ = state.e;
  h_ = state.h;
  l_ = state.l;
  f_ = state.f;
  sp_ = state.sp;
  halt_mode_ = state.halt_mode;
}

uint8_t LR35902::Read8(uint16_t addr) {
  return memory_->Read8(addr);
}

void LR35902::Write8(uint16_t addr, uint8_t val) {
  memory_->Write8(addr, val);
}

uint16_t LR35902::Read16(uint16_t addr) {
  return memory_->Read16(addr);
}

void LR35902::Write16(uint16_t addr, uint16_t val) {
  memory_->Write16(addr, val);
}

void LR35902::Push8(uint8_t val) {
  sp_--;
  Write8(sp_, val);
}

uint8_t LR35902::Pop8() {
  uint8_t val = Read8(sp_);
  sp_++;
  return val;
}

void LR35902::Push16(uint16_t val) {
  Push8(static_cast<uint8_t>((val >> 8) & 0xFF));
  Push8(static_cast<uint8_t>(val & 0xFF));
}

uint16_t LR35902::Pop16() {
  uint16_t low = Pop8();
  uint16_t high = Pop8();
  return (high << 8) | low;
}

uint32_t LR35902::Execute(uint8_t opcode) {
  switch (opcode) {
    case 0x00:  // NOP
      return 4;

    case 0xC3: {  // JP addr16
      uint16_t addr = Read16(pc_);
      pc_ += 2;
      pc_ = addr;
      return 16;
    }

    case 0xCD: {  // CALL addr16
      uint16_t addr = Read16(pc_);
      pc_ += 2;
      Push16(pc_);
      pc_ = addr;
      return 24;
    }

    case 0xC9:  // RET
      pc_ = Pop16();
      return 16;

    case 0x76:  // HALT
      halt_mode_ = true;
      return 4;

    case 0x20: {  // JR NZ, r8
      int8_t offset = static_cast<int8_t>(Read8(pc_));
      pc_++;
      if (!GetFlagZ()) {
        pc_ = static_cast<uint16_t>(pc_ + offset);
        return 12;
      }
      return 8;
    }

    case 0x18: {  // JR r8 (unconditional)
      int8_t offset = static_cast<int8_t>(Read8(pc_));
      pc_++;
      pc_ = static_cast<uint16_t>(pc_ + offset);
      return 12;
    }

    case 0x3E: {  // LD A, u8
      a_ = Read8(pc_);
      pc_++;
      return 8;
    }

    case 0xC7:  // RST 0x00
      Push16(pc_);
      pc_ = 0x00;
      return 16;

    default:  // NOP for unknown opcodes
      return 4;
  }
}

}  // namespace GBEmulator
