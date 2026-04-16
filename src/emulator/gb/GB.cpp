#include "emulator/gb/GB.h"

namespace GBEmulator {

GB::GB()
    : cpu_(&memory_),
      ppu_(&memory_),
      apu_(&memory_),
      running_(false),
      last_frame_count_(0) {
  // Wire peripherals into memory after construction
  memory_.Init(&cartridge_, &ppu_, &apu_);
}

void GB::Load(const std::string& rom_path) {
  cartridge_.Load(rom_path);
  Reset();
}

void GB::Load(std::span<const uint8_t> rom_data) {
  cartridge_.Load(rom_data);
  Reset();
}

void GB::Reset() {
  cpu_.Reset();
  running_ = true;
  last_frame_count_ = ppu_.GetFrameCount();
}

uint32_t GB::Step() {
  if (!running_) return 0;

  // Execute one CPU instruction
  uint32_t cpu_cycles = cpu_.Step();

  // Tick PPU for each CPU cycle (1 CPU cycle ≈ 4 master cycles)
  for (uint32_t i = 0; i < cpu_cycles; ++i) {
    ppu_.Tick();
  }

  // Tick APU for each CPU cycle
  for (uint32_t i = 0; i < cpu_cycles; ++i) {
    apu_.Tick();
  }

  return cpu_cycles;
}

void GB::RunFrame() {
  last_frame_count_ = ppu_.GetFrameCount();

  while (ppu_.GetFrameCount() == last_frame_count_ && running_) {
    Step();
  }
}

GB::State GB::SaveState() const {
  return State{
    .cpu = cpu_.SaveState(),
    .ppu = ppu_.SaveState(),
    .memory = memory_.SaveState(),
    .cartridge = cartridge_.SaveState(),
    .apu = apu_.SaveState(),
    .running = running_,
  };
}

void GB::LoadState(const State& state) {
  cpu_.LoadState(state.cpu);
  ppu_.LoadState(state.ppu);
  memory_.LoadState(state.memory);
  cartridge_.LoadState(state.cartridge);
  apu_.LoadState(state.apu);
  running_ = state.running;
  last_frame_count_ = ppu_.GetFrameCount();
}

}  // namespace GBEmulator
