#pragma once
// Atari 2600 memory map
//   0x0000–0x007F  TIA write (mirrored every 128 bytes in lower 4KB)
//   0x0080–0x00FF  PIA RAM (128 bytes)
//   0x0280–0x029F  PIA I/O & timer registers
//   0x1000–0x1FFF  Cartridge ROM (4 KB window, may be bank-switched)

#include <cstdint>
#include <vector>

namespace Atari2600 {

class TIA;
class PIA6532;

class Atari2600Memory {
public:
    Atari2600Memory(TIA& tia, PIA6532& pia) noexcept;
    ~Atari2600Memory() = default;

    Atari2600Memory(const Atari2600Memory&)            = delete;
    Atari2600Memory& operator=(const Atari2600Memory&) = delete;

    void LoadROM(const std::vector<uint8_t>& rom);

    uint8_t Read8(uint16_t addr);
    void    Write8(uint16_t addr, uint8_t val);

    // Read without side effects (for debuggers)
    uint8_t Peek(uint16_t addr) const noexcept;

private:
    TIA&     tia_;
    PIA6532& pia_;

    std::vector<uint8_t> rom_;   // Up to 32 KB (bank-switched carts)
    uint8_t bankOffset_ = 0;     // Active 4-KB bank for larger ROMs

    // Resolve cartridge read address with bank switching (F8, F6, FE mappers)
    uint8_t CartRead(uint16_t addr);
    void    CartWrite(uint16_t addr, uint8_t val);
};

} // namespace Atari2600
