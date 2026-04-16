#pragma once

#include "ISaveStateable.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace AIO::Emulator::Common {

/// ── SaveStateWriter ──────────────────────────────────────────────────────────
///
/// Sequential byte sink for save-state serialization.
///
/// Subsystems call WriteU8/U16/U32/U64/Bool/Bytes to emit their state.
/// The BusMap record the emitted bytes, which can later be read back by
/// a SaveStateReader. The writer appends; it never overwrites.
///
/// All multi-byte values are written in little-endian order regardless of
/// the host platform, so save files are portable across architectures.
class SaveStateWriter {
public:
    SaveStateWriter()  = default;
    ~SaveStateWriter() = default;

    void WriteU8  (uint8_t  v);
    void WriteU16 (uint16_t v);
    void WriteU32 (uint32_t v);
    void WriteU64 (uint64_t v);
    void WriteBool(bool     v);
    void WriteBytes(const uint8_t* src, size_t len);
    void WriteString(std::string_view s);

    /// Return the accumulated byte buffer (ready to persist to disk or RAM).
    [[nodiscard]] const std::vector<uint8_t>& Buffer() const noexcept { return buffer_; }

    /// Number of bytes written so far.
    [[nodiscard]] size_t Size() const noexcept { return buffer_.size(); }

private:
    std::vector<uint8_t> buffer_;
};

/// ── SaveStateReader ───────────────────────────────────────────────────────────
///
/// Sequential byte source for save-state deserialization.
///
/// Reads must be issued in the same order as the corresponding writes.
/// If the buffer is exhausted before all expected reads complete,
/// every read method throws std::out_of_range.
class SaveStateReader {
public:
    /// @param data  Buffer to read from. Must outlive this reader.
    explicit SaveStateReader(const std::vector<uint8_t>& data) noexcept;

    [[nodiscard]] uint8_t  ReadU8 ();
    [[nodiscard]] uint16_t ReadU16();
    [[nodiscard]] uint32_t ReadU32();
    [[nodiscard]] uint64_t ReadU64();
    [[nodiscard]] bool     ReadBool();
    void ReadBytes(uint8_t* dst, size_t len);
    [[nodiscard]] std::string ReadString();

    /// Bytes remaining in the buffer.
    [[nodiscard]] size_t Remaining() const noexcept;

    /// True if all bytes have been consumed.
    [[nodiscard]] bool AtEnd() const noexcept { return Remaining() == 0; }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_ = 0;

    void EnsureBytes(size_t n) const;
};

/// ── SaveStateManager ─────────────────────────────────────────────────────────
///
/// High-level coordinator for console save-state snapshots.
///
/// The console passes an ordered list of ISaveStateable components to
/// SaveAll(); the manager serializes them in order with section headers so
/// that LoadAll() can verify structural integrity before restoring.
///
/// File format (version 1):
///   [4 bytes] magic: 0x41494F53  ("AIOS")
///   [4 bytes] format version: 1
///   [4 bytes] section count
///   for each section:
///     [2 bytes] name length
///     [N bytes] section name (UTF-8)
///     [4 bytes] payload length
///     [N bytes] ISaveStateable::SaveState() output
class SaveStateManager {
public:
    static constexpr uint32_t kMagic   = 0x41494F53u; // "AIOS"
    static constexpr uint32_t kVersion = 1u;

    SaveStateManager()  = default;
    ~SaveStateManager() = default;

    /// Serialize @p components into a binary blob.
    ///
    /// @param components  Ordered list of (name, component) pairs.
    ///                    Every pointer must be non-null.
    /// @return            Serialized snapshot.
    /// @throws std::invalid_argument if any component pointer is null.
    [[nodiscard]] std::vector<uint8_t> SaveAll(
        const std::vector<std::pair<std::string, ISaveStateable*>>& components) const;

    /// Restore @p components from a blob previously produced by SaveAll().
    ///
    /// @param data        Blob from SaveAll().
    /// @param components  Ordered list matching the original SaveAll() call.
    /// @throws std::runtime_error if the magic, version, or section structure is invalid.
    void LoadAll(
        const std::vector<uint8_t>&                                  data,
        const std::vector<std::pair<std::string, ISaveStateable*>>&  components);
};

} // namespace AIO::Emulator::Common
