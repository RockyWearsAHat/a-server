#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class R3000A;
class PS1GPU;
class PS1SPU;
class PS1DMA;
class PS1Timer;
class InterruptController;
class CDROM;
class PS1Controller;
class PS1;

class PS1Memory : public Common::Loggable {
public:
  PS1Memory();
  ~PS1Memory() = default;

  void Reset();
  bool LoadBIOS(const std::string &path);

  // ─── Component References ───────────────────────────────────────────
  void SetCPU(R3000A *cpu) { this->cpu = cpu; }
  void SetGPU(PS1GPU *gpu) { this->gpu = gpu; }
  void SetSPU(PS1SPU *spu) { this->spu = spu; }
  void SetDMA(PS1DMA *dma) { this->dma = dma; }
  void SetTimers(PS1Timer *timers) { this->timers = timers; }
  void SetInterrupts(InterruptController *irq) { this->interrupts = irq; }
  void SetCDROM(CDROM *cdrom) { this->cdrom = cdrom; }
  void SetController(PS1Controller *ctrl) { this->controller = ctrl; }
  void SetPS1(PS1 *ps1) { this->ps1 = ps1; }

  // ─── Bus Read/Write ─────────────────────────────────────────────────
  uint8_t Read8(uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);

  void Write8(uint32_t addr, uint8_t value);
  void Write16(uint32_t addr, uint16_t value);
  void Write32(uint32_t addr, uint32_t value);

  // ─── Direct RAM Access (for DMA, tests, debugger) ───────────────────
  uint32_t ReadRAM32(uint32_t offset) const;
  void WriteRAM32(uint32_t offset, uint32_t value);
  uint8_t *GetRAMPointer() { return ram.data(); }
  const uint8_t *GetRAMPointer() const { return ram.data(); }

  // Direct BIOS access for tests
  void WriteBIOS32(uint32_t offset, uint32_t value);

  // ─── Scratchpad Access ──────────────────────────────────────────────
  uint32_t ReadScratchpad32(uint32_t offset) const;
  void WriteScratchpad32(uint32_t offset, uint32_t value);

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

  // Cache isolation state (controlled by COP0 SR.Isc)
  bool IsCacheIsolated() const { return cacheIsolated; }
  void SetCacheIsolated(bool isolated) { cacheIsolated = isolated; }

private:
  // ─── Memory Regions ─────────────────────────────────────────────────
  std::vector<uint8_t> ram;
  std::vector<uint8_t> bios;
  std::vector<uint8_t> scratchpad;
  std::vector<uint8_t> expansion1;
  std::vector<uint8_t> expansion2;

  // ─── Component Pointers ─────────────────────────────────────────────
  R3000A *cpu = nullptr;
  PS1GPU *gpu = nullptr;
  PS1SPU *spu = nullptr;
  PS1DMA *dma = nullptr;
  PS1Timer *timers = nullptr;
  InterruptController *interrupts = nullptr;
  CDROM *cdrom = nullptr;
  PS1Controller *controller = nullptr;
  PS1 *ps1 = nullptr;

  bool cacheIsolated = false;
  bool biosLoaded = false;

  // ─── Address Translation ────────────────────────────────────────────
  uint32_t TranslateAddress(uint32_t virtualAddr) const;

  // ─── I/O Register Dispatch ──────────────────────────────────────────
  uint32_t ReadIO32(uint32_t addr);
  uint16_t ReadIO16(uint32_t addr);
  uint8_t ReadIO8(uint32_t addr);
  void WriteIO32(uint32_t addr, uint32_t value);
  void WriteIO16(uint32_t addr, uint16_t value);
  void WriteIO8(uint32_t addr, uint8_t value);

  // Memory control registers (mostly ignored, but stored for accuracy)
  std::array<uint32_t, 9> memCtrl1{};
  uint32_t ramSizeReg = 0;
  uint32_t cacheCtrlReg = 0;
};

} // namespace AIO::Emulator::PS1
