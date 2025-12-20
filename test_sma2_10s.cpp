#include "emulator/gba/GBA.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace AIO::Emulator::GBA;

static std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data((size_t)size);
    if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    return data;
}

static void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to write file: " + path.string());
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    }
}

static std::string HexBlock8(const std::vector<uint8_t>& save, size_t offset) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < 8; ++i) {
        if (offset + i >= save.size()) break;
        oss << std::setw(2) << (int)save[offset + i];
    }
    return oss.str();
}

static bool IsReasonableDumpRegion(uint32_t addr) {
    // ROM, EWRAM, IWRAM, and IO are the most useful for this investigation.
    if (addr >= 0x08000000 && addr < 0x0A000000) return true; // ROM/WS
    if (addr >= 0x02000000 && addr < 0x03000000) return true; // EWRAM
    if (addr >= 0x03000000 && addr < 0x04000000) return true; // IWRAM
    if (addr >= 0x04000000 && addr < 0x04000400) return true; // IO
    return false;
}

static void DumpMemHex(GBA& gba, uint32_t addr, size_t bytes, const char* label) {
    if (!IsReasonableDumpRegion(addr)) {
        std::cerr << "[HARNESS] Skip dump " << label << " addr=0x" << std::hex << addr << std::dec << "\n";
        return;
    }

    const uint32_t base = addr & ~0xFu;
    const size_t dumpBytes = (bytes + 15u) & ~15u;
    std::cerr << "[HARNESS] Dump " << label << " @0x" << std::hex << base << std::dec
              << " (" << dumpBytes << " bytes)\n";

    for (size_t row = 0; row < dumpBytes; row += 16) {
        const uint32_t rowAddr = base + (uint32_t)row;
        std::cerr << "  0x" << std::hex << std::setw(8) << std::setfill('0') << rowAddr << ": ";
        for (int i = 0; i < 16; ++i) {
            const uint32_t a = rowAddr + (uint32_t)i;
            const uint32_t w = gba.ReadMem32(a & ~3u);
            const uint8_t b = (uint8_t)((w >> ((a & 3u) * 8u)) & 0xFFu);
            std::cerr << std::setw(2) << (int)b << " ";
        }
        std::cerr << std::dec << "\n";
    }
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path workspace = std::filesystem::current_path();

        const std::filesystem::path romPath = (argc >= 2) ? std::filesystem::path(argv[1]) : (workspace / "SMA2.gba");
        const std::filesystem::path refSavPath = (argc >= 3)
            ? std::filesystem::path(argv[2])
            : (workspace / "SMA2.sav.mgba_reference");
        const int seconds = (argc >= 4) ? std::stoi(argv[3]) : 10;

        if (!std::filesystem::exists(romPath)) {
            std::cerr << "ROM not found: " << romPath << "\n";
            return 2;
        }
        if (!std::filesystem::exists(refSavPath)) {
            std::cerr << "Reference save not found: " << refSavPath << "\n";
            return 3;
        }

        // Stage into a temp dir so we don't mutate repo saves.
        const auto tmpBase = std::filesystem::temp_directory_path() / "aio_sma2_headless";
        std::filesystem::create_directories(tmpBase);
        const auto runDir = tmpBase / ("run_" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(runDir);

        const auto stagedRom = runDir / "SMA2.gba";
        const auto stagedSav = runDir / "SMA2.sav";
        std::filesystem::copy_file(romPath, stagedRom, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(refSavPath, stagedSav, std::filesystem::copy_options::overwrite_existing);

        const auto refSav = ReadFile(stagedSav);

        GBA gba;
        if (!gba.LoadROM(stagedRom.string())) {
            std::cerr << "Failed to load ROM\n";
            return 4;
        }

        // Optional breakpoint support (uses existing ARM7TDMI debugger plumbing).
        // Example:
        //   AIO_BREAK_PC=0x0809E1CC AIO_STEPBACK=25 ./build/bin/SMA2Harness ...
        const char* breakPcEnv = std::getenv("AIO_BREAK_PC");
        const char* stepBackEnv = std::getenv("AIO_STEPBACK");
        uint32_t breakPc = 0;
        int stepBackCount = 0;
        if (breakPcEnv && *breakPcEnv) {
            breakPc = (uint32_t)std::strtoul(breakPcEnv, nullptr, 0);
            gba.AddBreakpoint(breakPc);
            if (stepBackEnv && *stepBackEnv) {
                stepBackCount = std::max(0, std::stoi(stepBackEnv));
            }
            std::cerr << "[HARNESS] Breakpoint armed at pc=0x" << std::hex << breakPc << std::dec
                      << " stepBack=" << stepBackCount << "\n";
        }

        // Run for N seconds of emulated time.
        constexpr int CYCLES_PER_SECOND = 16780000; // ~16.78 MHz
        const int64_t targetCycles = (int64_t)seconds * CYCLES_PER_SECOND;
        int64_t cycles = 0;

        // Optional PC sampling to spot tight loops / stalls.
        // Tunables:
        // - AIO_PC_SAMPLE_CYCLES: sample period in cycles (default 200000)
        // - AIO_PC_STALL_SAMPLES: consecutive identical PC samples before we dump state (default 200)
        const char* sampleEnv = std::getenv("AIO_PC_SAMPLE_CYCLES");
        const int64_t sampleEvery = sampleEnv ? std::stoll(sampleEnv) : 200000; // ~0.012s
        const char* stallEnv = std::getenv("AIO_PC_STALL_SAMPLES");
        const int stallThreshold = stallEnv ? std::stoi(stallEnv) : 200; // ~2.4s with default sampleEvery

        uint32_t lastSamplePc = 0xFFFFFFFFu;
        int samePcSamples = 0;
        std::map<uint32_t, int> pcHistogram;

        while (cycles < targetCycles && !gba.IsCPUHalted() && !gba.IsHalted()) {
            const int stepCycles = gba.Step();
            cycles += stepCycles;

            if (sampleEvery > 0 && (cycles % sampleEvery) < stepCycles) {
                const uint32_t pc = gba.GetPC();
                pcHistogram[pc]++;

                if (pc == lastSamplePc) {
                    samePcSamples++;
                } else {
                    lastSamplePc = pc;
                    samePcSamples = 0;
                }

                if (samePcSamples == stallThreshold) {
                    std::cerr << "[STALL DETECTED] pc=0x" << std::hex << pc << std::dec
                              << " thumb=" << (gba.IsThumbMode() ? 1 : 0)
                              << " cycles=" << cycles
                              << " sampleEvery=" << sampleEvery
                              << " samples=" << stallThreshold
                              << "\n";
                    gba.DumpCPUState(std::cerr);
                    break;
                }
            }
        }

        if (breakPc != 0 && gba.IsHalted()) {
            std::cerr << "[HARNESS] Breakpoint hit at pc=0x" << std::hex << gba.GetPC() << std::dec
                      << " thumb=" << (gba.IsThumbMode() ? 1 : 0) << "\n";

            if (stepBackCount > 0) {
                for (int i = 0; i < stepBackCount; ++i) {
                    gba.StepBack();
                }
                std::cerr << "[HARNESS] After StepBack(" << stepBackCount << ") pc=0x" << std::hex << gba.GetPC() << std::dec
                          << " thumb=" << (gba.IsThumbMode() ? 1 : 0) << "\n";
            }

            gba.DumpCPUState(std::cerr);

            // Dump memory around likely pointers used by the save-validation routine.
            // This is the fastest way to see what tables/structures are involved.
            const uint32_t r0 = gba.GetRegister(0);
            const uint32_t r1 = gba.GetRegister(1);
            const uint32_t r2 = gba.GetRegister(2);
            const uint32_t r3 = gba.GetRegister(3);
            const uint32_t r5 = gba.GetRegister(5);
            const uint32_t sp = gba.GetRegister(13);
            const uint32_t lr = gba.GetRegister(14);

            DumpMemHex(gba, r0, 0x80, "R0");
            DumpMemHex(gba, r1, 0x80, "R1");
            DumpMemHex(gba, r2, 0x80, "R2");
            DumpMemHex(gba, r3, 0x80, "R3");
            DumpMemHex(gba, r5, 0x80, "R5");
            DumpMemHex(gba, sp, 0x80, "SP");
            DumpMemHex(gba, lr & ~1u, 0x40, "LR(code)");

            // Also capture key IO registers often used in piracy/boot/save checks.
            const uint16_t waitcnt = gba.ReadMem16(0x04000204);
            const uint16_t ime = gba.ReadMem16(0x04000208);
            const uint16_t ie = gba.ReadMem16(0x04000200);
            const uint16_t iflg = gba.ReadMem16(0x04000202);
            const uint16_t postflg = gba.ReadMem16(0x04000300);
            std::cerr << "[HARNESS] IO WAITCNT=0x" << std::hex << waitcnt
                      << " IME=0x" << ime
                      << " IE=0x" << ie
                      << " IF=0x" << iflg
                      << " POSTFLG=0x" << postflg
                      << std::dec << "\n";
        }

        gba.SaveGame();
        const auto outSav = ReadFile(stagedSav);

        const size_t block2 = 2 * 8;
        const size_t block4 = 4 * 8;

        std::cout << "Ran ~" << seconds << "s (" << cycles << " cycles).\n";

        if (!pcHistogram.empty()) {
            std::vector<std::pair<uint32_t, int>> top(pcHistogram.begin(), pcHistogram.end());
            std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            const size_t n = std::min<size_t>(5, top.size());
            std::cout << "Top PCs:";
            for (size_t i = 0; i < n; ++i) {
                std::cout << " 0x" << std::hex << std::setw(8) << std::setfill('0') << top[i].first
                          << std::dec << "(" << top[i].second << ")";
            }
            std::cout << "\n";
        }

        std::cout << "ref  block2=" << HexBlock8(refSav, block2) << " block4=" << HexBlock8(refSav, block4) << "\n";
        std::cout << "out  block2=" << HexBlock8(outSav, block2) << " block4=" << HexBlock8(outSav, block4) << "\n";

        if (refSav.size() == outSav.size()) {
            size_t diffCount = 0;
            std::vector<size_t> firstDiffs;
            firstDiffs.reserve(32);
            for (size_t i = 0; i < refSav.size(); ++i) {
                if (refSav[i] != outSav[i]) {
                    diffCount++;
                    if (firstDiffs.size() < 32) firstDiffs.push_back(i);
                }
            }
            std::cout << "diff bytes=" << diffCount << " of " << refSav.size() << "\n";
            if (!firstDiffs.empty()) {
                std::cout << "first diffs:";
                for (size_t off : firstDiffs) {
                    std::cout << " 0x" << std::hex << std::setw(4) << std::setfill('0') << off
                              << ":" << std::setw(2) << (int)refSav[off]
                              << "->" << std::setw(2) << (int)outSav[off] << std::dec;
                }
                std::cout << "\n";
            }
        } else {
            std::cout << "diff bytes=unknown (size mismatch ref=" << refSav.size() << " out=" << outSav.size() << ")\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
