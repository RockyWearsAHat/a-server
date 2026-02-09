#include "emulator/ps1/PS1.h"
#include "emulator/common/Logger.h"
#include "emulator/ps1/PS1HleBios.h"
#include <fstream>

namespace AIO::Emulator::PS1 {

PS1::PS1() {
  // Construct components in dependency order
  interrupts = std::make_unique<InterruptController>();
  memory = std::make_unique<PS1Memory>();
  gpu = std::make_unique<PS1GPU>(*memory);
  spu = std::make_unique<PS1SPU>(*memory);
  dma = std::make_unique<PS1DMA>(*memory, *gpu, *spu, *interrupts);
  timers = std::make_unique<PS1Timer>(*interrupts);
  cdrom = std::make_unique<CDROM>(*memory, *interrupts);
  controller = std::make_unique<PS1Controller>(*interrupts);
  gte = std::make_unique<GTE>();
  cpu = std::make_unique<R3000A>(*memory);

  // Wire DMA to CDROM (setter injection — CDROM depends on DMA and vice versa)
  dma->SetCDROM(cdrom.get());

  // Wire PS1Memory to all subsystems it dispatches I/O to
  memory->SetCPU(cpu.get());
  memory->SetGPU(gpu.get());
  memory->SetSPU(spu.get());
  memory->SetDMA(dma.get());
  memory->SetTimers(timers.get());
  memory->SetInterrupts(interrupts.get());
  memory->SetCDROM(cdrom.get());
  memory->SetController(controller.get());
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

  bool wasInVBlank = gpu->InVBlank();
  uint32_t prevScanline = gpu->GetScanline();
  gpu->Tick(cpuCycles);

  // Fire VBlank IRQ on transition into VBlank
  if (!wasInVBlank && gpu->InVBlank()) {
    interrupts->RequestIRQ(IRQ::VBLANK);
    // Deliver HLE VBlank event so games using WaitEvent/TestEvent can proceed
    PS1HleBios::DeliverEvent(0xF0000001, 0x0001);
  }

  // Fire HBlank tick for timers on each new scanline
  if (gpu->GetScanline() != prevScanline) {
    timers->TickHBlank();
  }

  // Dot clock for timers
  uint32_t dots = cpuCycles * 8;
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

  // DEBUG: log IRQ state after 1M steps to see state during game loop
  static uint64_t irqDbgCount = 0;
  irqDbgCount++;
  if (irqDbgCount == 1000000 || irqDbgCount == 5000000 ||
      irqDbgCount == 20000000) {
    uint32_t sr = cpu->GetSR();
    uint32_t istat = interrupts->ReadStat();
    uint32_t imask = interrupts->ReadMask();
    auto &log = AIO::Emulator::Common::Logger::Instance();
    char buf[256];
    snprintf(buf, sizeof(buf),
             "IRQ-DBG[%llu]: SR=0x%08X CAUSE=0x%08X I_STAT=0x%04X "
             "I_MASK=0x%04X pending=%d IEc=%d IM2=%d",
             (unsigned long long)irqDbgCount, sr, cause, istat, imask,
             interrupts->HasPendingIRQ() ? 1 : 0, (sr & 1) ? 1 : 0,
             (sr & (1u << 10)) ? 1 : 0);
    log.Log(AIO::Emulator::Common::LogLevel::Info, "PS1.IRQ", buf);
  }

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
