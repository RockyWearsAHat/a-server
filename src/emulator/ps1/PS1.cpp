#include "emulator/ps1/PS1.h"
#include "emulator/common/Logger.h"
#include "emulator/ps1/PS1HleBios.h"
#include "emulator/ps1/PS1MDEC.h"
#include <fstream>

namespace AIO::Emulator::PS1 {

PS1::PS1() {
  // Construct components in dependency order
  interrupts = std::make_unique<InterruptController>();
  memory = std::make_unique<PS1Memory>();
  gpu = std::make_unique<PS1GPU>(*memory);
  spu = std::make_unique<PS1SPU>(*memory, *interrupts);
  dma = std::make_unique<PS1DMA>(*memory, *gpu, *spu, *interrupts);
  mdec = std::make_unique<PS1MDEC>();
  timers = std::make_unique<PS1Timer>(*interrupts);
  cdrom = std::make_unique<CDROM>(*memory, *interrupts);
  controller = std::make_unique<PS1Controller>(*interrupts);
  gte = std::make_unique<GTE>();
  cpu = std::make_unique<R3000A>(*memory);

  // Wire DMA to CDROM (setter injection — CDROM depends on DMA and vice versa)
  dma->SetCDROM(cdrom.get());
  dma->SetMDEC(mdec.get());

  // Wire SPU to CDROM for XA-ADPCM audio decode
  cdrom->SetSPU(spu.get());

  // Wire PS1Memory to all subsystems it dispatches I/O to
  memory->SetCPU(cpu.get());
  memory->SetGPU(gpu.get());
  memory->SetSPU(spu.get());
  memory->SetDMA(dma.get());
  memory->SetTimers(timers.get());
  memory->SetInterrupts(interrupts.get());
  memory->SetCDROM(cdrom.get());
  memory->SetController(controller.get());
  memory->SetMDEC(mdec.get());
  memory->SetPS1(this);

  // Wire GTE into CPU for COP2 operations
  cpu->SetGTE(gte.get());

  // Wire PS1 back-reference for HLE BIOS dispatch
  cpu->SetPS1(this);

  controller->SetControllerConnected(true);
}

PS1::~PS1() = default;

bool PS1::LoadBIOS(const std::string &path) {
  biosLoaded = memory->LoadBIOS(path);
  return biosLoaded;
}

bool PS1::LoadDisc(const std::string &path) {
  // Verify file exists
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return false;
  file.close();

  cdrom->LoadDisc(path);
  discLoaded = true;
  return true;
}

bool PS1::InitHLE() {
  if (!discLoaded) {
    return false;
  }
  Reset();
  biosLoaded = PS1HleBios::InitHLE(*this);
  return biosLoaded;
}

void PS1::Reset() {
  cpu->Reset();
  memory->Reset();
  gpu->Reset();
  spu->Reset();
  dma->Reset();
  interrupts->Reset();
  timers->Reset();
  cdrom->Reset();
  controller->Reset();
  gte->Reset();
  totalCyclesExecuted.store(0, std::memory_order_relaxed);
}

int PS1::Step() {
  int cpuCycles = cpu->Step();
  // Account for DMA transfers that stalled the CPU during this step
  cpuCycles += static_cast<int>(dma->GetAndClearPendingCycles());

  bool wasInVBlank = gpu->InVBlank();
  uint32_t prevScanline = gpu->GetScanline();
  gpu->Tick(cpuCycles);

  // Propagate VBlank state to timer sync logic each step
  timers->SetVBlankState(gpu->InVBlank());

  // Fire VBlank IRQ on transition into VBlank
  if (!wasInVBlank && gpu->InVBlank()) {
    gpu->LatchDisplayBuffer();
    interrupts->RequestIRQ(IRQ::VBLANK);
    static int vblankCount = 0;
    if (++vblankCount <= 5) {
      auto &log = AIO::Emulator::Common::Logger::Instance();
      log.LogFmt(AIO::Emulator::Common::LogLevel::Info, "PS1",
                 "VBlank #%d at scanline=%u cycle=%llu iMask=%08X", vblankCount,
                 gpu->GetScanline(), totalCyclesExecuted.load(),
                 interrupts->ReadMask());
    }
  }

  // Fire HBlank tick for timers on each new scanline
  if (gpu->GetScanline() != prevScanline) {
    timers->TickHBlank();
  }

  // Dot clock for Timer0: DOTS_PER_SCANLINE / CPU_CYCLES_PER_SCANLINE ≈ 1.57
  dotClockAccum += cpuCycles * Clock::DOTS_PER_SCANLINE_NTSC;
  uint32_t dots = dotClockAccum / Clock::CPU_CYCLES_PER_SCANLINE_NTSC;
  dotClockAccum %= Clock::CPU_CYCLES_PER_SCANLINE_NTSC;
  timers->TickDotClock(dots);

  // Tick other subsystems
  timers->Tick(cpuCycles);
  spu->Tick(cpuCycles);
  cdrom->Tick(cpuCycles);
  controller->Tick(cpuCycles);

  // Mirror external IRQ state into COP0 CAUSE IP bit 2 (hardware IRQ line)
  uint32_t cause = cpu->GetCause();
  if (interrupts->HasPendingIRQ()) {
    cause |= (1u << 10); // IP2 — hardware interrupt pending
  } else {
    cause &= ~(1u << 10);
  }
  cpu->SetCOP0(CPU::COP0::CAUSE, cause);

  if (cpu->IsInterruptPending()) {
    cpu->TriggerInterrupt();
  }

  totalCyclesExecuted.fetch_add(cpuCycles, std::memory_order_relaxed);
  return cpuCycles;
}

void PS1::UpdateInput(uint16_t buttonState) {
  controller->SetButtonState(buttonState);
}

} // namespace AIO::Emulator::PS1
