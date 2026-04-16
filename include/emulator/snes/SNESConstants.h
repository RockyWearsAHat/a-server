#pragma once

#include <cstdint>

namespace AIO::Emulator::SNES {

static constexpr uint32_t kMasterClockHz = 21477272u;

static constexpr uint32_t kWramBase = 0x7E0000u;
static constexpr uint32_t kWramSize = 0x20000u; // 128 KB

static constexpr uint32_t kRomMaxSize = 0x800000u; // 8 MB

static constexpr int kPpuVisibleWidth = 256;
static constexpr int kPpuVisibleHeight = 224;
static constexpr int kPpuCyclesPerLine = 341;
static constexpr int kPpuLinesPerFrame = 262;

} // namespace AIO::Emulator::SNES
