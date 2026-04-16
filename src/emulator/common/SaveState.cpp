// SaveState.cpp — save-state serialization/deserialization implementation.
//
// All multi-byte values serialized as little-endian for portability.
// String length is serialized as a 2-byte prefix (max 65535 chars).

#include "emulator/common/SaveState.h"

#include <cstring>
#include <stdexcept>

namespace AIO::Emulator::Common {

// ── SaveStateWriter ────────────────────────────────────────────────────────────

void SaveStateWriter::WriteU8(uint8_t v) {
    buffer_.push_back(v);
}

void SaveStateWriter::WriteU16(uint16_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
}

void SaveStateWriter::WriteU32(uint32_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
    buffer_.push_back(static_cast<uint8_t>(v >> 16));
    buffer_.push_back(static_cast<uint8_t>(v >> 24));
}

void SaveStateWriter::WriteU64(uint64_t v) {
    WriteU32(static_cast<uint32_t>(v));
    WriteU32(static_cast<uint32_t>(v >> 32));
}

void SaveStateWriter::WriteBool(bool v) {
    WriteU8(v ? 1u : 0u);
}

void SaveStateWriter::WriteBytes(const uint8_t* src, size_t len) {
    if (src == nullptr && len > 0)
        throw std::invalid_argument("SaveStateWriter::WriteBytes: src must not be null when len > 0");

    buffer_.insert(buffer_.end(), src, src + len);
}

void SaveStateWriter::WriteString(std::string_view s) {
    if (s.size() > 65535)
        throw std::invalid_argument("SaveStateWriter::WriteString: string too long (max 65535)");
    WriteU16(static_cast<uint16_t>(s.size()));
    buffer_.insert(buffer_.end(),
                   reinterpret_cast<const uint8_t*>(s.data()),
                   reinterpret_cast<const uint8_t*>(s.data()) + s.size());
}

// ── SaveStateReader ────────────────────────────────────────────────────────────

SaveStateReader::SaveStateReader(const std::vector<uint8_t>& data) noexcept
    : data_(data), pos_(0) {}

void SaveStateReader::EnsureBytes(size_t n) const {
    if (pos_ + n > data_.size())
        throw std::out_of_range("SaveStateReader: unexpected end of save-state buffer");
}

size_t SaveStateReader::Remaining() const noexcept {
    return data_.size() - pos_;
}

uint8_t SaveStateReader::ReadU8() {
    EnsureBytes(1);
    return data_[pos_++];
}

uint16_t SaveStateReader::ReadU16() {
    EnsureBytes(2);
    uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                 (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
}

uint32_t SaveStateReader::ReadU32() {
    EnsureBytes(4);
    uint32_t v = static_cast<uint32_t>(data_[pos_])        |
                 (static_cast<uint32_t>(data_[pos_ + 1]) << 8)  |
                 (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                 (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
}

uint64_t SaveStateReader::ReadU64() {
    uint64_t lo = ReadU32();
    uint64_t hi = ReadU32();
    return lo | (hi << 32);
}

bool SaveStateReader::ReadBool() {
    return ReadU8() != 0;
}

void SaveStateReader::ReadBytes(uint8_t* dst, size_t len) {
    if (dst == nullptr && len > 0)
        throw std::invalid_argument("SaveStateReader::ReadBytes: dst must not be null when len > 0");
    EnsureBytes(len);
    std::memcpy(dst, data_.data() + pos_, len);
    pos_ += len;
}

std::string SaveStateReader::ReadString() {
    uint16_t len = ReadU16();
    EnsureBytes(len);
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), len);
    pos_ += len;
    return s;
}

// ── SaveStateManager ───────────────────────────────────────────────────────────

std::vector<uint8_t> SaveStateManager::SaveAll(
    const std::vector<std::pair<std::string, ISaveStateable*>>& components) const
{
    for (const auto& [name, comp] : components)
        if (comp == nullptr)
            throw std::invalid_argument(
                "SaveStateManager::SaveAll: component '" + name + "' is null");

    SaveStateWriter outer;
    outer.WriteU32(kMagic);
    outer.WriteU32(kVersion);
    outer.WriteU32(static_cast<uint32_t>(components.size()));

    for (const auto& [name, comp] : components) {
        // Serialize the component into a temporary buffer to get its length.
        SaveStateWriter section;
        comp->SaveState(section);
        const auto& payload = section.Buffer();

        outer.WriteString(name);
        outer.WriteU32(static_cast<uint32_t>(payload.size()));
        outer.WriteBytes(payload.data(), payload.size());
    }

    return outer.Buffer();
}

void SaveStateManager::LoadAll(
    const std::vector<uint8_t>&                                  data,
    const std::vector<std::pair<std::string, ISaveStateable*>>&  components)
{
    SaveStateReader reader(data);

    uint32_t magic = reader.ReadU32();
    if (magic != kMagic)
        throw std::runtime_error("SaveStateManager::LoadAll: invalid magic number");

    uint32_t version = reader.ReadU32();
    if (version != kVersion)
        throw std::runtime_error("SaveStateManager::LoadAll: unsupported version " +
                                 std::to_string(version));

    uint32_t count = reader.ReadU32();
    if (count != static_cast<uint32_t>(components.size()))
        throw std::runtime_error(
            "SaveStateManager::LoadAll: section count mismatch (expected " +
            std::to_string(components.size()) + ", got " + std::to_string(count) + ")");

    for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        std::string sectionName = reader.ReadString();
        if (sectionName != components[i].first)
            throw std::runtime_error(
                "SaveStateManager::LoadAll: section name mismatch (expected '" +
                components[i].first + "', got '" + sectionName + "')");

        uint32_t payloadLen = reader.ReadU32();
        std::vector<uint8_t> payload(payloadLen);
        reader.ReadBytes(payload.data(), payloadLen);

        SaveStateReader sectionReader(payload);
        components[i].second->LoadState(sectionReader);
    }
}

} // namespace AIO::Emulator::Common
