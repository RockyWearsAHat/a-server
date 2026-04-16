#pragma once

#include "IBusDevice.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace AIO::Emulator::Common {

/// Maps an address space to IBusDevice handlers using sorted, non-overlapping regions.
///
/// BusMap<A> is the address router for a console. Each console instantiates
/// one (or more) BusMap with the appropriate address type (uint16_t for
/// 8-bit systems, uint32_t for 32-bit systems) and registers its RAM,
/// ROM, and MMIO devices. Reads and writes are dispatched in O(log n) time.
///
/// Design rules:
///   - Regions may not overlap. AddRegion() throws if they do.
///   - The full address space does NOT need to be mapped; unmapped reads
///     return 0xFF (open-bus) and unmapped writes are silent no-ops.
///   - Endianness for multi-byte accesses is caller-specified via the
///     template parameter LittleEndian (true = LE, false = BE).
///
/// @tparam AddressT    Address type: uint16_t or uint32_t.
/// @tparam LittleEndian  true for little-endian bus (GBA, x86);
///                       false for big-endian bus (68000, MIPS).
template <typename AddressT, bool LittleEndian = true>
class BusMap {
    static_assert(std::is_same_v<AddressT, uint16_t> ||
                  std::is_same_v<AddressT, uint32_t>,
                  "BusMap address type must be uint16_t or uint32_t");

public:
    BusMap()  = default;
    ~BusMap() = default;

    BusMap(const BusMap&)            = delete;
    BusMap& operator=(const BusMap&) = delete;

    // ── Region registration ───────────────────────────────────────────────

    /// Map [base, base + size) to @p device.
    ///
    /// @param base    First address in the region. Must be <= max - size + 1.
    /// @param size    Number of bytes. Must be > 0.
    /// @param device  Non-owning pointer; must outlive this BusMap. Must not be null.
    /// @throws std::invalid_argument if size == 0 or device == nullptr.
    /// @throws std::logic_error      if the region overlaps an existing mapping.
    void AddRegion(AddressT base, AddressT size, IBusDevice* device);

    // ── Access API ────────────────────────────────────────────────────────

    /// @return Byte at @p address, or 0xFF if unmapped.
    [[nodiscard]] uint8_t  Read8 (AddressT address);

    /// @return 16-bit value at @p address assembled from two Read8 calls
    ///         in the bus's native byte order.
    [[nodiscard]] uint16_t Read16(AddressT address);

    /// @return 32-bit value at @p address assembled from four Read8 calls
    ///         in the bus's native byte order.
    [[nodiscard]] uint32_t Read32(AddressT address);

    void Write8 (AddressT address, uint8_t  value);
    void Write16(AddressT address, uint16_t value);
    void Write32(AddressT address, uint32_t value);

    // ── Diagnostics ───────────────────────────────────────────────────────

    /// Return the device name responsible for @p address, or "<unmapped>".
    [[nodiscard]] std::string_view DeviceAt(AddressT address) const;

private:
    struct Region {
        AddressT   base;
        AddressT   end;   // exclusive: [base, end)
        IBusDevice* device;

        bool Contains(AddressT addr) const noexcept {
            return addr >= base && addr < end;
        }
    };

    std::vector<Region> regions_; // sorted by base address

    /// Binary-search helper. Returns pointer to Region, or nullptr if unmapped.
    const Region* Lookup(AddressT address) const noexcept;
          Region* Lookup(AddressT address) noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Template implementation (must live in the header)
// ─────────────────────────────────────────────────────────────────────────────

template <typename A, bool LE>
void BusMap<A, LE>::AddRegion(A base, A size, IBusDevice* device) {
    if (size == 0)
        throw std::invalid_argument("BusMap::AddRegion: size must be > 0");
    if (device == nullptr)
        throw std::invalid_argument("BusMap::AddRegion: device must not be null");

    A end = static_cast<A>(base + size);

    // Check for overlap with every existing region.
    for (const auto& r : regions_) {
        if (base < r.end && end > r.base) {
            throw std::logic_error(
                std::string("BusMap::AddRegion: region [") +
                std::to_string(base) + ", " + std::to_string(end) +
                ") overlaps existing region mapped to " +
                std::string(r.device->DeviceName()));
        }
    }

    // Insert in sorted order by base address for binary-search lookup.
    auto insertPos = std::lower_bound(regions_.begin(), regions_.end(), base,
        [](const Region& r, A addr) { return r.base < addr; });
    regions_.insert(insertPos, Region{ base, end, device });
}

template <typename A, bool LE>
const typename BusMap<A, LE>::Region* BusMap<A, LE>::Lookup(A address) const noexcept {
    // Binary search: find the last region whose base <= address.
    auto it = std::upper_bound(regions_.begin(), regions_.end(), address,
        [](A addr, const Region& r) { return addr < r.base; });
    if (it == regions_.begin())
        return nullptr;
    --it;
    return it->Contains(address) ? &(*it) : nullptr;
}

template <typename A, bool LE>
typename BusMap<A, LE>::Region* BusMap<A, LE>::Lookup(A address) noexcept {
    return const_cast<Region*>(
        static_cast<const BusMap<A, LE>*>(this)->Lookup(address));
}

template <typename A, bool LE>
uint8_t BusMap<A, LE>::Read8(A address) {
    if (const Region* r = Lookup(address))
        return r->device->Read8(address);
    return 0xFF; // open-bus
}

template <typename A, bool LE>
uint16_t BusMap<A, LE>::Read16(A address) {
    if constexpr (LE) {
        return static_cast<uint16_t>(Read8(address)) |
               (static_cast<uint16_t>(Read8(static_cast<A>(address + 1))) << 8);
    } else {
        return (static_cast<uint16_t>(Read8(address)) << 8) |
                static_cast<uint16_t>(Read8(static_cast<A>(address + 1)));
    }
}

template <typename A, bool LE>
uint32_t BusMap<A, LE>::Read32(A address) {
    if constexpr (LE) {
        return static_cast<uint32_t>(Read8(address)) |
               (static_cast<uint32_t>(Read8(static_cast<A>(address + 1))) << 8) |
               (static_cast<uint32_t>(Read8(static_cast<A>(address + 2))) << 16) |
               (static_cast<uint32_t>(Read8(static_cast<A>(address + 3))) << 24);
    } else {
        return (static_cast<uint32_t>(Read8(address))                       << 24) |
               (static_cast<uint32_t>(Read8(static_cast<A>(address + 1)))   << 16) |
               (static_cast<uint32_t>(Read8(static_cast<A>(address + 2)))   << 8)  |
                static_cast<uint32_t>(Read8(static_cast<A>(address + 3)));
    }
}

template <typename A, bool LE>
void BusMap<A, LE>::Write8(A address, uint8_t value) {
    if (Region* r = Lookup(address))
        r->device->Write8(address, value);
    // Unmapped write is a silent no-op (open bus).
}

template <typename A, bool LE>
void BusMap<A, LE>::Write16(A address, uint16_t value) {
    if constexpr (LE) {
        Write8(address,                        static_cast<uint8_t>(value));
        Write8(static_cast<A>(address + 1),   static_cast<uint8_t>(value >> 8));
    } else {
        Write8(address,                        static_cast<uint8_t>(value >> 8));
        Write8(static_cast<A>(address + 1),   static_cast<uint8_t>(value));
    }
}

template <typename A, bool LE>
void BusMap<A, LE>::Write32(A address, uint32_t value) {
    if constexpr (LE) {
        Write8(address,                        static_cast<uint8_t>(value));
        Write8(static_cast<A>(address + 1),   static_cast<uint8_t>(value >> 8));
        Write8(static_cast<A>(address + 2),   static_cast<uint8_t>(value >> 16));
        Write8(static_cast<A>(address + 3),   static_cast<uint8_t>(value >> 24));
    } else {
        Write8(address,                        static_cast<uint8_t>(value >> 24));
        Write8(static_cast<A>(address + 1),   static_cast<uint8_t>(value >> 16));
        Write8(static_cast<A>(address + 2),   static_cast<uint8_t>(value >> 8));
        Write8(static_cast<A>(address + 3),   static_cast<uint8_t>(value));
    }
}

template <typename A, bool LE>
std::string_view BusMap<A, LE>::DeviceAt(A address) const {
    if (const Region* r = Lookup(address))
        return r->device->DeviceName();
    return "<unmapped>";
}

} // namespace AIO::Emulator::Common
