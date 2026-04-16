#include "emulator/n64/N64Memory.h"
#include "emulator/n64/N64Cartridge.h"
#include "emulator/n64/RDP.h"
#include "emulator/n64/RSP.h"
#include "emulator/n64/N64Constants.h"

namespace N64Emulator {

N64Memory::N64Memory()
    : cart_(nullptr), rdp_(nullptr), rsp_(nullptr) {
  rdram_.resize(kRdramSize);           // 8 MB RDRAM
  mi_regs_.resize(4);
  vi_regs_.resize(14);
  ai_regs_.resize(6);
  pi_regs_.resize(13);
  si_regs_.resize(6);
}

void N64Memory::Init(N64Cartridge* cart, RDP* rdp, RSP* rsp) {
  cart_ = cart;
  rdp_  = rdp;
  rsp_  = rsp;
}

uint8_t N64Memory::PhysRead8(uint32_t paddr) {
  if (paddr < kRdramSize) {
    return rdram_[paddr];
  }
  if (paddr >= kCartRomBase && paddr < kCartRomBase + kCartRomSize) {
    if (cart_) return cart_->Read8(paddr - kCartRomBase);
    return 0xFF;
  }
  if (paddr >= kRspDmemBase && paddr < kRspDmemBase + kRspDmemSize) {
    return 0xFF;  // RSP DMEM — not fully mapped yet
  }
  return 0xFF;
}

uint16_t N64Memory::PhysRead16(uint32_t paddr) {
  uint8_t hi = PhysRead8(paddr);
  uint8_t lo = PhysRead8(paddr + 1);
  return (hi << 8) | lo;
}

uint32_t N64Memory::PhysRead32(uint32_t paddr) {
  uint8_t b0 = PhysRead8(paddr);
  uint8_t b1 = PhysRead8(paddr + 1);
  uint8_t b2 = PhysRead8(paddr + 2);
  uint8_t b3 = PhysRead8(paddr + 3);
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

void N64Memory::PhysWrite8(uint32_t paddr, uint8_t val) {
  if (paddr < kRdramSize) {
    rdram_[paddr] = val;
  }
  // Other regions: scaffold silently ignores writes
}

void N64Memory::PhysWrite16(uint32_t paddr, uint16_t val) {
  PhysWrite8(paddr,     static_cast<uint8_t>((val >> 8) & 0xFF));
  PhysWrite8(paddr + 1, static_cast<uint8_t>(val & 0xFF));
}

void N64Memory::PhysWrite32(uint32_t paddr, uint32_t val) {
  PhysWrite8(paddr,     static_cast<uint8_t>((val >> 24) & 0xFF));
  PhysWrite8(paddr + 1, static_cast<uint8_t>((val >> 16) & 0xFF));
  PhysWrite8(paddr + 2, static_cast<uint8_t>((val >> 8) & 0xFF));
  PhysWrite8(paddr + 3, static_cast<uint8_t>(val & 0xFF));
}

N64Memory::State N64Memory::SaveState() const {
  return State{
    .rdram    = rdram_,
    .mi_regs  = mi_regs_,
    .vi_regs  = vi_regs_,
    .ai_regs  = ai_regs_,
    .pi_regs  = pi_regs_,
    .si_regs  = si_regs_,
  };
}

void N64Memory::LoadState(const State& state) {
  rdram_   = state.rdram;
  mi_regs_ = state.mi_regs;
  vi_regs_ = state.vi_regs;
  ai_regs_ = state.ai_regs;
  pi_regs_ = state.pi_regs;
  si_regs_ = state.si_regs;
}

}  // namespace N64Emulator
