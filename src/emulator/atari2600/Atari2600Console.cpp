#include "emulator/atari2600/Atari2600Console.h"
#include "emulator/atari2600/TIA.h"
#include "emulator/atari2600/PIA6532.h"
#include "emulator/atari2600/Atari2600Memory.h"
#include "emulator/atari2600/MOS6507.h"
#include <fstream>

namespace Atari2600 {

Atari2600Console::Atari2600Console()
    : tia_(std::make_unique<TIA>())
    , pia_(std::make_unique<PIA6532>())
    , mem_(std::make_unique<Atari2600Memory>(*tia_, *pia_))
    , cpu_(std::make_unique<MOS6507>(*mem_))
    , romLoaded_(false)
{}

Atari2600Console::~Atari2600Console() = default;

bool Atari2600Console::LoadROM(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    const auto size = file.tellg();
    if (size <= 0) return false;

    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) return false;

    mem_->LoadROM(data);
    romLoaded_ = true;
    Reset();
    return true;
}

void Atari2600Console::Reset() {
    tia_->Reset();
    pia_->Reset();
    cpu_->Reset();
}

void Atari2600Console::Step() {
    if (!romLoaded_) return;

    const int cpuCycles = cpu_->Step();

    // TIA ticks at 3× the CPU rate (color clocks)
    for (int i = 0; i < cpuCycles * 3; ++i) {
        tia_->Tick();
    }

    // PIA ticks once per CPU cycle
    for (int i = 0; i < cpuCycles; ++i) {
        pia_->Tick();
    }
}

void Atari2600Console::RunFrame() {
    if (!romLoaded_) return;

    tia_->ClearFrameReady();
    while (!tia_->IsFrameReady()) {
        Step();
    }
}

const uint32_t* Atari2600Console::GetFramebuffer() const noexcept {
    return tia_->GetFramebuffer();
}

void Atari2600Console::SetJoystick(int port, uint8_t state) noexcept {
    // Atari 2600: joystick 1 = Port A bits [3:0], joystick 2 = bits [7:4].
    // Active-low: 0 = pressed, 1 = released.
    const uint8_t invNibble = static_cast<uint8_t>((~state) & 0x0F);
    const bool firePressed = (state & 0x10u) != 0;
    if (port == 0) {
        portA_ = static_cast<uint8_t>((portA_ & 0xF0u) | invNibble);
    } else {
        portA_ = static_cast<uint8_t>((portA_ & 0x0Fu) | (invNibble << 4u));
    }
    pia_->SetPortA(portA_);
    tia_->SetTrigger(port, firePressed);
}

} // namespace Atari2600
