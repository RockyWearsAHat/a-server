#pragma once

#include "emulator/common/Fuzzable.h"
#include "emulator/common/Loggable.h"
#include <vector>
#include <functional>

namespace AIO::Emulator::Common {

class Fuzzer : public Loggable {
public:
    Fuzzer();
    
    // Run fuzzing on a target component
    // stepFunc: Function to execute one step/instruction of the component
    void Run(Fuzzable* target, std::function<void()> stepFunc, int iterations);

private:
    bool DetectLoop(uint32_t pc);
    
    std::vector<uint32_t> m_pcHistory;
    static const int HISTORY_SIZE = 1024;
};

} // namespace AIO::Emulator::Common
