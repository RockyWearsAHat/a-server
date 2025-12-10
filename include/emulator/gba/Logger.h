#pragma once

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <functional>

namespace AIO::Emulator::GBA {

enum class LogCategory : uint32_t {
    None = 0,
    CPU = 1 << 0,
    IRQ = 1 << 1,
    DMA = 1 << 2,
    Video = 1 << 3,
    Audio = 1 << 4,
    Mem = 1 << 5,
    // ... (rest of enum and class definition)
};

// ... (rest of Logger class definition)

} // namespace AIO::Emulator::GBA
