#include "emulator/nes/NES.h"
#include "emulator/common/SaveState.h"
#include <cstring>

namespace AIO::Emulator::NES {

NES::NES()
    : cart_(std::make_unique<NESCartridge>())
    , mem_ (std::make_unique<NESMemory>())
    , cpu_ (std::make_unique<RP2A03>(*mem_))
    , ppu_ (std::make_unique<PPU2C02>(*cart_))
    , apu_ (std::make_unique<APU2A03>())
{}

void NES::Load(std::span<const uint8_t> romData) {
    cart_->Load(romData); // throws std::invalid_argument on failure
    mem_->Init(cart_.get());

    // Wire PPU register callbacks: NESMemory forwards $2000–$2007 to PPU
    mem_->SetPpuCallbacks(
        [this](uint8_t reg) -> uint8_t { return ppu_->ReadRegister(reg); },
        [this](uint8_t reg, uint8_t val) { ppu_->WriteRegister(reg, val); }
    );

    // Wire APU register callbacks: NESMemory forwards $4000–$4017 to APU
    mem_->SetApuCallbacks(
        [this](uint16_t addr) -> uint8_t { return apu_->ReadRegister(addr); },
        [this](uint16_t addr, uint8_t val) {
            if (addr == 0x4014) {
                HandleOamDma(val);
            } else {
                apu_->WriteRegister(addr, val);
            }
        }
    );

    // PPU fires NMI into CPU via the NMI line
    ppu_->SetNmiCallback([this]() { cpu_->SetNMI(true); });

    // APU fires IRQ into CPU via the IRQ line
    apu_->SetIrqCallback([this]() { cpu_->SetIRQ(true); });

    Reset();
}

void NES::Reset() {
    cart_->Reset();
    mem_->Reset();
    cpu_->Reset();
    ppu_->Reset();
    apu_->Reset();
    oamDmaStallCycles_    = 0;
    totalMasterCycles_    = 0;
}

int NES::Step() {
    // Consume OAM DMA stall one CPU cycle at a time
    if (oamDmaStallCycles_ > 0) {
        --oamDmaStallCycles_;
        // Tick PPU and APU even during DMA stall
        ppu_->Tick(3);
        apu_->Tick(1);
        totalMasterCycles_ += 12; // 12 master clocks per CPU cycle (NTSC)
        return 1;
    }

    const int cycles = cpu_->Step();

    // Clear NMI edge after CPU services it
    cpu_->SetNMI(false);
    cpu_->SetIRQ(apu_->IrqPending() || cart_->IrqPending());

    // PPU ticks at 3× CPU rate (NTSC)
    ppu_->Tick(static_cast<uint32_t>(cycles * 3));

    // APU ticks 1:1 with CPU
    apu_->Tick(static_cast<uint32_t>(cycles));

    totalMasterCycles_ += static_cast<uint64_t>(cycles) * 12;

    return cycles;
}

void NES::RunFrame() {
    const uint64_t startFrame = ppu_->FrameCount();
    while (ppu_->FrameCount() == startFrame) {
        static_cast<void>(Step());
    }
}

void NES::HandleOamDma(uint8_t page) {
    // OAM DMA stall: 513 cycles if current cycle is even, 514 if odd
    oamDmaStallCycles_ = ((totalMasterCycles_ / 12) & 1) ? 514 : 513;
    oamDmaStallCycles_ -= 1; // already accounting for current cycle

    // Copy 256 bytes from CPU memory page into OAM
    const uint16_t base = static_cast<uint16_t>(page) << 8;
    uint8_t oamPage[256];
    for (int i = 0; i < 256; ++i) {
        oamPage[i] = mem_->Read8(static_cast<uint32_t>(base + i));
    }
    ppu_->OamDma(oamPage);
}

void NES::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    cart_->SaveState(w);
    mem_->SaveState(w);
    cpu_->SaveState(w);
    ppu_->SaveState(w);
    apu_->SaveState(w);
    w.WriteU64(totalMasterCycles_.load());
    w.WriteU32(static_cast<uint32_t>(oamDmaStallCycles_));
}

void NES::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    cart_->LoadState(r);
    mem_->LoadState(r);
    cpu_->LoadState(r);
    ppu_->LoadState(r);
    apu_->LoadState(r);
    totalMasterCycles_ = r.ReadU64();
    oamDmaStallCycles_ = static_cast<int>(r.ReadU32());
}

} // namespace AIO::Emulator::NES
