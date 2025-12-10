#pragma once

#include <cstdint>
#include <string>

namespace AIO::Emulator::Common {

class Fuzzable {
public:
    virtual ~Fuzzable() = default;

    // Inject random values into the component's state (registers, memory, etc.)
    virtual void InjectRandomState() = 0;

    // Reset the component to a known clean state
    virtual void ResetToKnownState() = 0;

    // Get a hash of the current state (for consistency checking)
    virtual uint64_t GetStateHash() const = 0;
    
    // Get the current Program Counter (if applicable, else 0)
    // Used for loop detection
    virtual uint32_t GetPC() const { return 0; }
};

} // namespace AIO::Emulator::Common
