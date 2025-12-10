#include "emulator/common/Fuzzer.h"
#include <iostream>
#include <random>
#include <csignal>
#include <setjmp.h>

namespace AIO::Emulator::Common {

Fuzzer::Fuzzer() : Loggable("Fuzzer") {
    m_pcHistory.reserve(HISTORY_SIZE);
}

void Fuzzer::Run(Fuzzable* target, std::function<void()> stepFunc, int iterations) {
    LogInfo("Starting fuzzing run for %d iterations", iterations);

    target->ResetToKnownState();
    target->InjectRandomState();

    m_pcHistory.clear();

    for (int i = 0; i < iterations; ++i) {
        try {
            // Periodically re-inject random state to explore new paths
            if (i % 1000 == 0) {
                target->InjectRandomState();
                m_pcHistory.clear();
            }

            uint32_t pc = target->GetPC();
            if (DetectLoop(pc)) {
                LogWarn("Infinite loop detected at PC=0x%08X after %d steps. Resetting state.", pc, i);
                target->ResetToKnownState();
                target->InjectRandomState();
                m_pcHistory.clear();
                continue;
            }

            // Log the current instruction and its effects
            LogInfo("Step %d: Executing instruction at PC=0x%08X", i, pc);
            stepFunc();

            // Optionally, log the state of key registers or memory
            LogDebug("State after step %d: PC=0x%08X", i, target->GetPC());
        } catch (const std::exception& e) {
            LogError("Exception caught during fuzzing at step %d: %s", i, e.what());
            target->ResetToKnownState();
        } catch (...) {
            LogError("Unknown exception caught during fuzzing at step %d", i);
            target->ResetToKnownState();
        }
    }

    LogInfo("Fuzzing run completed");
}

bool Fuzzer::DetectLoop(uint32_t pc) {
    m_pcHistory.push_back(pc);
    if (m_pcHistory.size() > HISTORY_SIZE) {
        m_pcHistory.erase(m_pcHistory.begin());
    }
    
    // Simple loop detection: Check if the last N PCs are identical (tight loop)
    // or if we see a repeating pattern.
    // For now, let's just detect tight loops (same PC for 100 times)
    if (m_pcHistory.size() >= 100) {
        bool allSame = true;
        for (size_t i = m_pcHistory.size() - 100; i < m_pcHistory.size(); ++i) {
            if (m_pcHistory[i] != pc) {
                allSame = false;
                break;
            }
        }
        if (allSame) return true;
    }
    
    return false;
}

} // namespace AIO::Emulator::Common
