#pragma once

#include <cstdint>
#include <string_view>

namespace AIO::Emulator::Common {

/// Interface for any device that lives on an address bus.
///
/// A bus device occupies a contiguous address region and responds to byte-wide
/// reads and writes. Wider (16-bit, 32-bit) accesses are assembled by the
/// BusMap from successive 8-bit calls in the platform's native byte order.
///
/// Implementors MUST:
///   - Accept any address in the range [mappedBase, mappedBase + mappedSize).
///   - Be deterministic: same address/value sequence → same state outcome.
///   - Validate nothing about the address (BusMap enforces range; the device
///     only needs to mask or sub-index into its internal arrays).
///
/// Implementors MUST NOT:
///   - Read from or write to other devices (bus transactions belong to the bus).
///   - Store raw pointers to other devices inside Read8/Write8 paths to prevent
///     cyclic dependency chains that violate single-responsibility.
class IBusDevice {
public:
    virtual ~IBusDevice() = default;

    /// Read one byte from @p address.
    /// @param address  Physical address on the bus (not a device-local offset).
    [[nodiscard]] virtual uint8_t Read8(uint32_t address) = 0;

    /// Write one byte to @p address.
    /// @param address  Physical address on the bus.
    /// @param value    Byte to write.
    virtual void Write8(uint32_t address, uint8_t value) = 0;

    /// Identifier for logging and debugging (e.g. "WorkRAM", "VRAM").
    [[nodiscard]] virtual std::string_view DeviceName() const = 0;
};

} // namespace AIO::Emulator::Common
