#pragma once

#include <cstdint>
#include <string>

namespace AIO::Emulator::GBA {

class Fuzzer {
public:
    static void RunCPUFuzz(int iterations);
    static void RunMemFuzz(const std::string& romPath, int iterations);
};

} // namespace AIO::Emulator::GBA
