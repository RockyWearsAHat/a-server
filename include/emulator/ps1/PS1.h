#pragma once

#include "CDROM.h"
#include "GTE.h"
#include "InterruptController.h"
#include "PS1Constants.h"
#include "PS1Controller.h"
#include "PS1DMA.h"
#include "PS1GPU.h"
#include "PS1Memory.h"
#include "PS1SPU.h"
#include "PS1Timer.h"
#include "R3000A.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace AIO::Emulator::PS1 {

class PS1 {
public:
  PS1();
  ~PS1();

  bool LoadBIOS(const std::string &path);
  bool LoadDisc(const std::string &path);
  void Reset();
  int Step(); // Run one CPU instruction, returns cycles consumed

  void UpdateInput(uint16_t buttonState);
  bool IsBIOSLoaded() const { return biosLoaded; }

  // ─── Component Access ───────────────────────────────────────────────
  R3000A &GetCPU() { return *cpu; }
  const R3000A &GetCPU() const { return *cpu; }
  PS1Memory &GetMemory() { return *memory; }
  const PS1Memory &GetMemory() const { return *memory; }
  PS1GPU &GetGPU() { return *gpu; }
  const PS1GPU &GetGPU() const { return *gpu; }
  PS1SPU &GetSPU() { return *spu; }
  PS1DMA &GetDMA() { return *dma; }
  InterruptController &GetInterrupts() { return *interrupts; }
  PS1Timer &GetTimers() { return *timers; }
  CDROM &GetCDROM() { return *cdrom; }
  PS1Controller &GetController() { return *controller; }
  GTE &GetGTE() { return *gte; }

  // Debug helpers
  uint32_t ReadMem32(uint32_t addr) { return memory->Read32(addr); }
  void WriteMem32(uint32_t addr, uint32_t value) {
    memory->Write32(addr, value);
  }
  uint32_t GetPC() const { return cpu->GetPC(); }

  uint64_t GetTotalCycles() const {
    return totalCyclesExecuted.load(std::memory_order_relaxed);
  }

  // ─── Framebuffer for display ────────────────────────────────────────
  const uint16_t *GetFramebuffer() const { return gpu->GetFramebuffer(); }
  uint32_t GetDisplayWidth() const { return gpu->GetDisplayWidth(); }
  uint32_t GetDisplayHeight() const { return gpu->GetDisplayHeight(); }

private:
  std::unique_ptr<R3000A> cpu;
  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<PS1GPU> gpu;
  std::unique_ptr<PS1SPU> spu;
  std::unique_ptr<PS1DMA> dma;
  std::unique_ptr<InterruptController> interrupts;
  std::unique_ptr<PS1Timer> timers;
  std::unique_ptr<CDROM> cdrom;
  std::unique_ptr<PS1Controller> controller;
  std::unique_ptr<GTE> gte;

  bool biosLoaded = false;
  bool discLoaded = false;

  std::atomic<uint64_t> totalCyclesExecuted{0};
};

} // namespace AIO::Emulator::PS1
