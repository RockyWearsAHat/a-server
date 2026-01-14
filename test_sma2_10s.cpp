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
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace AIO::Emulator::GBA;

static std::vector<uint8_t> ReadFile(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }
  std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> data((size_t)size);
  if (size > 0 && !f.read(reinterpret_cast<char *>(data.data()), size)) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }
  return data;
}

static void WriteFile(const std::filesystem::path &path,
                      const std::vector<uint8_t> &data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
  if (!data.empty()) {
    f.write(reinterpret_cast<const char *>(data.data()),
            (std::streamsize)data.size());
  }
}

static std::string HexBlock8(const std::vector<uint8_t> &save, size_t offset) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < 8; ++i) {
    if (offset + i >= save.size())
      break;
    oss << std::setw(2) << (int)save[offset + i];
  }
  return oss.str();
}

static bool IsReasonableDumpRegion(uint32_t addr) {
  // ROM, EWRAM, IWRAM, and IO are the most useful for this investigation.
  if (addr >= 0x08000000 && addr < 0x0A000000)
    return true; // ROM/WS
  if (addr >= 0x02000000 && addr < 0x03000000)
    return true; // EWRAM
  if (addr >= 0x03000000 && addr < 0x04000000)
    return true; // IWRAM
  if (addr >= 0x04000000 && addr < 0x04000400)
    return true; // IO
  return false;
}

static void DumpMemHex(GBA &gba, uint32_t addr, size_t bytes,
                       const char *label) {
  if (!IsReasonableDumpRegion(addr)) {
    std::cerr << "[HARNESS] Skip dump " << label << " addr=0x" << std::hex
              << addr << std::dec << "\n";
    return;
  }

  const uint32_t base = addr & ~0xFu;
  const size_t dumpBytes = (bytes + 15u) & ~15u;
  std::cerr << "[HARNESS] Dump " << label << " @0x" << std::hex << base
            << std::dec << " (" << dumpBytes << " bytes)\n";

  for (size_t row = 0; row < dumpBytes; row += 16) {
    const uint32_t rowAddr = base + (uint32_t)row;
    std::cerr << "  0x" << std::hex << std::setw(8) << std::setfill('0')
              << rowAddr << ": ";
    for (int i = 0; i < 16; ++i) {
      const uint32_t a = rowAddr + (uint32_t)i;
      const uint32_t w = gba.ReadMem32(a & ~3u);
      const uint8_t b = (uint8_t)((w >> ((a & 3u) * 8u)) & 0xFFu);
      std::cerr << std::setw(2) << (int)b << " ";
    }
    std::cerr << std::dec << "\n";
  }
}

static void DumpVideoSummary(GBA &gba) {
  auto r16 = [&](uint32_t addr) { return gba.ReadMem16(addr); };
  auto r32 = [&](uint32_t addr) { return gba.ReadMem32(addr); };

  const uint16_t dispcnt = r16(0x04000000);
  const uint16_t dispstat = r16(0x04000004);
  const uint16_t vcount = r16(0x04000006);
  const uint16_t bg0cnt = r16(0x04000008);
  const uint16_t bg1cnt = r16(0x0400000A);
  const uint16_t bg2cnt = r16(0x0400000C);
  const uint16_t bg3cnt = r16(0x0400000E);

  const uint16_t winin = r16(0x04000048);
  const uint16_t winout = r16(0x0400004A);
  const uint16_t bldcnt = r16(0x04000050);
  const uint16_t bldalpha = r16(0x04000052);
  const uint16_t bldy = r16(0x04000054);

  const uint16_t keyinput = r16(0x04000130);

  std::cout << "[VIDEO] DISPCNT=0x" << std::hex << dispcnt
            << " mode=" << std::dec << (dispcnt & 0x7)
            << " forcedBlank=" << (((dispcnt >> 7) & 1) ? 1 : 0)
            << " BG0=" << (((dispcnt >> 8) & 1) ? 1 : 0)
            << " BG1=" << (((dispcnt >> 9) & 1) ? 1 : 0)
            << " BG2=" << (((dispcnt >> 10) & 1) ? 1 : 0)
            << " BG3=" << (((dispcnt >> 11) & 1) ? 1 : 0)
            << " OBJ=" << (((dispcnt >> 12) & 1) ? 1 : 0)
            << " WIN0=" << (((dispcnt >> 13) & 1) ? 1 : 0)
            << " WIN1=" << (((dispcnt >> 14) & 1) ? 1 : 0)
            << " OBJWIN=" << (((dispcnt >> 15) & 1) ? 1 : 0) << "\n";

  std::cout << "[CPU] PC=0x" << std::hex << gba.GetPC() << std::dec
            << " thumb=" << (gba.IsThumbMode() ? 1 : 0)
            << " halted=" << (gba.IsCPUHalted() ? 1 : 0) << " cpsr=0x"
            << std::hex << gba.GetCPSR() << std::dec << " mode=0x" << std::hex
            << (gba.GetCPSR() & 0x1Fu) << std::dec << "\n";

  // Boot-critical IO state that games frequently poll during startup.
  const uint16_t waitcnt = r16(0x04000204);
  const uint16_t ie = r16(0x04000200);
  const uint16_t iflg = r16(0x04000202);
  const uint16_t ime = r16(0x04000208);
  const uint8_t postflg = (uint8_t)(r16(0x04000300) & 0xFFu);
  const uint16_t bios_if = r16(0x03007FF8);
  const uint32_t irq_handler_ptr = r32(0x03007FFC);
  std::cout << "[BOOT] WAITCNT=0x" << std::hex << waitcnt << " IE=0x" << ie
            << " IF=0x" << iflg << " IME=0x" << ime << " POSTFLG=0x"
            << (uint32_t)postflg << " BIOS_IF=0x" << bios_if << " IRQHAND=0x"
            << irq_handler_ptr << std::dec << "\n";

  // Minimal instruction window around current PC to identify tight loops.
  const uint32_t pc = gba.GetPC();
  if (gba.IsThumbMode()) {
    const uint32_t base = (pc & ~1u);
    std::cout << "[CPU] THUMB @0x" << std::hex << base << std::dec << ":";
    for (int i = -4; i <= 4; ++i) {
      const uint32_t a = base + (uint32_t)(i * 2);
      const uint16_t hw = gba.ReadMem16(a);
      std::cout << " " << "0x" << std::hex << std::setw(4) << std::setfill('0')
                << hw << std::dec;
    }
    std::cout << "\n";
  } else {
    const uint32_t base = (pc & ~3u);
    std::cout << "[CPU] ARM   @0x" << std::hex << base << std::dec << ":";
    for (int i = -2; i <= 2; ++i) {
      const uint32_t a = base + (uint32_t)(i * 4);
      const uint32_t w = r32(a);
      std::cout << " " << "0x" << std::hex << std::setw(8) << std::setfill('0')
                << w << std::dec;
    }
    std::cout << "\n";
  }

  std::cout << "[VIDEO] DISPSTAT=0x" << std::hex << dispstat << " VCOUNT=0x"
            << vcount << " BG0CNT=0x" << bg0cnt << " BG1CNT=0x" << bg1cnt
            << " BG2CNT=0x" << bg2cnt << " BG3CNT=0x" << bg3cnt << std::dec
            << "\n";

  std::cout << "[VIDEO] WININ=0x" << std::hex << winin << " WINOUT=0x" << winout
            << " BLDCNT=0x" << bldcnt << " BLDALPHA=0x" << bldalpha
            << " BLDY=0x" << bldy << std::dec << "\n";

  std::cout << "[VIDEO] KEYINPUT(game)=0x" << std::hex << keyinput << std::dec
            << "\n";

  // Palette sanity: backdrop color is palette[0]. If it remains 0x0000, "blank"
  // == black.
  const uint16_t pal0 = r16(0x05000000);
  const uint16_t pal1 = r16(0x05000002);
  const uint16_t pal2 = r16(0x05000004);
  const uint16_t pal3 = r16(0x05000006);
  std::cout << "[VIDEO] PAL[0..3]={0x" << std::hex << pal0 << ",0x" << pal1
            << ",0x" << pal2 << ",0x" << pal3 << "}" << std::dec << "\n";

  // Framebuffer activity
  const auto &fb = gba.GetPPU().GetFramebuffer();
  uint32_t xorHash = 0;
  uint32_t nonBlack = 0;
  uint32_t nonZero = 0;
  for (uint32_t px : fb) {
    xorHash ^= px;
    if (px != 0)
      nonZero++;
    if ((px & 0x00FFFFFFu) != 0)
      nonBlack++; // ignore alpha
  }
  std::cout << "[VIDEO] FB size=" << fb.size() << " nonZero=" << nonZero
            << " nonBlackRGB=" << nonBlack << " xor=0x" << std::hex << xorHash
            << std::dec << "\n";
}

static void DumpDMA3(GBA &gba) {
  auto r16 = [&](uint32_t addr) { return gba.ReadMem16(addr); };
  auto r32 = [&](uint32_t addr) { return gba.ReadMem32(addr); };

  const uint32_t dmasad = r32(0x040000D4);
  const uint32_t dmadad = r32(0x040000D8);
  const uint16_t dmacnt_l = r16(0x040000DC);
  const uint16_t dmacnt_h = r16(0x040000DE);

  std::cerr << "[HARNESS] DMA3 SAD=0x" << std::hex << dmasad << " DAD=0x"
            << dmadad << " CNT_L=0x" << dmacnt_l << " CNT_H=0x" << dmacnt_h
            << std::dec << "\n";
}

static void DumpSMA2SaveHeaderStaging(GBA &gba) {
  // Known staging area observed in traces for SMA2 save header.
  DumpMemHex(gba, 0x02000380, 0x100, "EWRAM[0x02000380..]");
}

static uint16_t KeyMaskFromName(const std::string &name) {
  // KEYINPUT bits (0 = pressed):
  // 0:A 1:B 2:Select 3:Start 4:Right 5:Left 6:Up 7:Down 8:R 9:L
  static const std::unordered_map<std::string, uint16_t> k = {
      {"A", 1u << 0},     {"B", 1u << 1},     {"SELECT", 1u << 2},
      {"START", 1u << 3}, {"RIGHT", 1u << 4}, {"LEFT", 1u << 5},
      {"UP", 1u << 6},    {"DOWN", 1u << 7},  {"R", 1u << 8},
      {"L", 1u << 9},
  };
  auto it = k.find(name);
  return (it == k.end()) ? 0 : it->second;
}

struct InputEvent {
  int64_t cycle = 0;
  uint16_t mask = 0;
  bool down = false; // down = pressed (bit cleared)
};

static std::optional<std::vector<InputEvent>>
LoadInputScript(const std::filesystem::path &path, int64_t cyclesPerSecond) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return std::nullopt;
  }

  std::vector<InputEvent> events;
  std::string line;
  int lineNo = 0;
  while (std::getline(f, line)) {
    lineNo++;
    // Strip comments
    const auto hash = line.find('#');
    if (hash != std::string::npos)
      line = line.substr(0, hash);

    std::istringstream iss(line);
    double ms = 0.0;
    std::string key;
    std::string action;
    if (!(iss >> ms >> key >> action)) {
      continue;
    }

    // Normalize
    for (char &c : key)
      c = (char)std::toupper((unsigned char)c);
    for (char &c : action)
      c = (char)std::toupper((unsigned char)c);

    const uint16_t mask = KeyMaskFromName(key);
    if (mask == 0) {
      std::cerr << "[HARNESS] Input script: unknown key '" << key
                << "' at line " << lineNo << "\n";
      continue;
    }

    const bool down =
        (action == "DOWN" || action == "PRESS" || action == "PRESSED");
    const bool up =
        (action == "UP" || action == "RELEASE" || action == "RELEASED");
    if (!down && !up) {
      std::cerr << "[HARNESS] Input script: unknown action '" << action
                << "' at line " << lineNo << "\n";
      continue;
    }

    const int64_t cycle = (int64_t)((ms / 1000.0) * (double)cyclesPerSecond);
    events.push_back(InputEvent{cycle, mask, down});
  }

  std::sort(events.begin(), events.end(),
            [](const InputEvent &a, const InputEvent &b) {
              if (a.cycle != b.cycle)
                return a.cycle < b.cycle;
              return (int)a.down > (int)b.down;
            });
  return events;
}

static void DumpEnabledBgTilemaps(GBA &gba) {
  const uint16_t dispcnt = gba.ReadMem16(0x04000000);
  const bool bgEnable[4] = {
      ((dispcnt >> 8) & 1) != 0,
      ((dispcnt >> 9) & 1) != 0,
      ((dispcnt >> 10) & 1) != 0,
      ((dispcnt >> 11) & 1) != 0,
  };

  for (int bg = 0; bg < 4; ++bg) {
    if (!bgEnable[bg])
      continue;
    const uint16_t bgcnt = gba.ReadMem16(0x04000008u + (uint32_t)bg * 2u);
    const uint32_t screenBase = (bgcnt >> 8) & 0x1Fu;
    const uint32_t baseAddr = 0x06000000u + screenBase * 2048u;

    uint32_t xorHash = 0;
    uint32_t nonZero = 0;
    for (uint32_t i = 0; i < 1024; ++i) {
      const uint16_t entry = gba.ReadMem16(baseAddr + i * 2u);
      xorHash ^= ((uint32_t)entry << (i & 15));
      if (entry != 0)
        nonZero++;
    }

    std::cout << "[BGMAP] BG" << bg << " BGCNT=0x" << std::hex << bgcnt
              << std::dec << " screenBase=" << screenBase << " base=0x"
              << std::hex << baseAddr << std::dec
              << " nonZeroEntries=" << nonZero << " xor=0x" << std::hex
              << xorHash << std::dec << "\n";

    // Print a small prefix to make it easier to visually compare runs.
    std::cout << "[BGMAP] BG" << bg << " first64:";
    for (uint32_t i = 0; i < 64; ++i) {
      const uint16_t entry = gba.ReadMem16(baseAddr + i * 2u);
      std::cout << " 0x" << std::hex << std::setw(4) << std::setfill('0')
                << entry << std::dec;
    }
    std::cout << "\n";
  }
}

int main(int argc, char **argv) {
  try {
    const std::filesystem::path workspace = std::filesystem::current_path();

    const std::filesystem::path romPath =
        (argc >= 2) ? std::filesystem::path(argv[1]) : (workspace / "SMA2.gba");
    const std::filesystem::path refSavPath =
        (argc >= 3) ? std::filesystem::path(argv[2])
                    : (workspace / "SMA2.sav.mgba_reference");
    const int seconds = (argc >= 4) ? std::stoi(argv[3]) : 10;
    const std::filesystem::path scriptPath =
        (argc >= 5) ? std::filesystem::path(argv[4]) : std::filesystem::path();

    if (!std::filesystem::exists(romPath)) {
      std::cerr << "ROM not found: " << romPath << "\n";
      return 2;
    }
    if (!std::filesystem::exists(refSavPath)) {
      std::cerr << "Reference save not found: " << refSavPath << "\n";
      return 3;
    }

    // Stage into a temp dir so we don't mutate repo saves.
    const auto tmpBase =
        std::filesystem::temp_directory_path() / "aio_sma2_headless";
    std::filesystem::create_directories(tmpBase);
    const auto runDir = tmpBase / ("run_" + std::to_string(std::time(nullptr)));
    std::filesystem::create_directories(runDir);

    const auto stagedRom = runDir / "SMA2.gba";
    const auto stagedSav = runDir / "SMA2.sav";
    std::filesystem::copy_file(
        romPath, stagedRom, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(
        refSavPath, stagedSav,
        std::filesystem::copy_options::overwrite_existing);

    const auto refSav = ReadFile(stagedSav);

    GBA gba;
    if (!gba.LoadROM(stagedRom.string())) {
      std::cerr << "Failed to load ROM\n";
      return 4;
    }

    // Optional breakpoint support (uses existing ARM7TDMI debugger plumbing).
    // Example:
    //   AIO_BREAK_PC=0x0809E1CC AIO_STEPBACK=25 ./build/bin/SMA2Harness ...
    const char *breakPcEnv = std::getenv("AIO_BREAK_PC");
    const char *stepBackEnv = std::getenv("AIO_STEPBACK");
    uint32_t breakPc = 0;
    int stepBackCount = 0;
    if (breakPcEnv && *breakPcEnv) {
      breakPc = (uint32_t)std::strtoul(breakPcEnv, nullptr, 0);
      gba.AddBreakpoint(breakPc);
      if (stepBackEnv && *stepBackEnv) {
        stepBackCount = std::max(0, std::stoi(stepBackEnv));
      }
      std::cerr << "[HARNESS] Breakpoint armed at pc=0x" << std::hex << breakPc
                << std::dec << " stepBack=" << stepBackCount << "\n";
    }

    // Run for N seconds of emulated time.
    constexpr int CYCLES_PER_SECOND = 16780000; // ~16.78 MHz
    const int64_t targetCycles = (int64_t)seconds * CYCLES_PER_SECOND;
    int64_t cycles = 0;

    uint16_t keyState = 0x03FFu; // all released
    std::vector<InputEvent> inputEvents;
    size_t nextInputEvent = 0;
    if (!scriptPath.empty()) {
      auto loaded = LoadInputScript(scriptPath, CYCLES_PER_SECOND);
      if (!loaded) {
        std::cerr << "[HARNESS] Failed to open input script: " << scriptPath
                  << "\n";
        return 5;
      }
      inputEvents = std::move(*loaded);
      std::cerr << "[HARNESS] Loaded input script: " << scriptPath << " ("
                << inputEvents.size() << " events)\n";
    }

    // Optional PC sampling to spot tight loops / stalls.
    // Tunables:
    // - AIO_PC_SAMPLE_CYCLES: sample period in cycles (default 200000)
    // - AIO_PC_STALL_SAMPLES: consecutive identical PC samples before we dump
    // state (default 200)
    const char *sampleEnv = std::getenv("AIO_PC_SAMPLE_CYCLES");
    const int64_t sampleEvery =
        sampleEnv ? std::stoll(sampleEnv) : 200000; // ~0.012s
    const char *stallEnv = std::getenv("AIO_PC_STALL_SAMPLES");
    const int stallThreshold =
        stallEnv ? std::stoi(stallEnv) : 200; // ~2.4s with default sampleEvery

    uint32_t lastSamplePc = 0xFFFFFFFFu;
    int samePcSamples = 0;
    std::map<uint32_t, int> pcHistogram;

    while (cycles < targetCycles && !gba.IsCPUHalted() && !gba.IsHalted()) {
      while (nextInputEvent < inputEvents.size() &&
             cycles >= inputEvents[nextInputEvent].cycle) {
        const auto &ev = inputEvents[nextInputEvent];
        if (ev.down) {
          keyState = (uint16_t)(keyState & ~ev.mask);
        } else {
          keyState = (uint16_t)(keyState | ev.mask);
        }
        gba.UpdateInput(keyState);
        nextInputEvent++;
      }

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
                    << " cycles=" << cycles << " sampleEvery=" << sampleEvery
                    << " samples=" << stallThreshold << "\n";
          gba.DumpCPUState(std::cerr);
          break;
        }
      }
    }

    // Note: internally, a debugger breakpoint may surface as a CPU-halt state
    // rather than the emulator-wide halted flag. Treat either as a breakpoint
    // stop.
    if (breakPc != 0 && (gba.IsHalted() || gba.IsCPUHalted())) {
      std::cerr << "[HARNESS] Breakpoint stop at pc=0x" << std::hex
                << gba.GetPC() << std::dec
                << " thumb=" << (gba.IsThumbMode() ? 1 : 0)
                << " IsCPUHalted=" << (gba.IsCPUHalted() ? 1 : 0)
                << " IsHalted=" << (gba.IsHalted() ? 1 : 0) << "\n";

      if (stepBackCount > 0) {
        for (int i = 0; i < stepBackCount; ++i) {
          gba.StepBack();
        }
        std::cerr << "[HARNESS] After StepBack(" << stepBackCount << ") pc=0x"
                  << std::hex << gba.GetPC() << std::dec
                  << " thumb=" << (gba.IsThumbMode() ? 1 : 0) << "\n";
      }

      gba.DumpCPUState(std::cerr);

      DumpDMA3(gba);
      DumpSMA2SaveHeaderStaging(gba);

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
      std::cerr << "[HARNESS] IO WAITCNT=0x" << std::hex << waitcnt << " IME=0x"
                << ime << " IE=0x" << ie << " IF=0x" << iflg << " POSTFLG=0x"
                << postflg << std::dec << "\n";
    }

    gba.SaveGame();
    const auto outSav = ReadFile(stagedSav);

    DumpVideoSummary(gba);

    if (std::getenv("AIO_DUMP_BG_MAPS") != nullptr) {
      DumpEnabledBgTilemaps(gba);
    }

    const size_t block2 = 2 * 8;
    const size_t block4 = 4 * 8;

    std::cout << "Ran ~" << seconds << "s (" << cycles << " cycles).\n";

    if (!pcHistogram.empty()) {
      std::vector<std::pair<uint32_t, int>> top(pcHistogram.begin(),
                                                pcHistogram.end());
      std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
      });
      const size_t n = std::min<size_t>(5, top.size());
      std::cout << "Top PCs:";
      for (size_t i = 0; i < n; ++i) {
        std::cout << " 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << top[i].first << std::dec << "(" << top[i].second << ")";
      }
      std::cout << "\n";
    }

    std::cout << "ref  block2=" << HexBlock8(refSav, block2)
              << " block4=" << HexBlock8(refSav, block4) << "\n";
    std::cout << "out  block2=" << HexBlock8(outSav, block2)
              << " block4=" << HexBlock8(outSav, block4) << "\n";

    if (refSav.size() == outSav.size()) {
      size_t diffCount = 0;
      std::vector<size_t> firstDiffs;
      firstDiffs.reserve(32);
      for (size_t i = 0; i < refSav.size(); ++i) {
        if (refSav[i] != outSav[i]) {
          diffCount++;
          if (firstDiffs.size() < 32)
            firstDiffs.push_back(i);
        }
      }
      std::cout << "diff bytes=" << diffCount << " of " << refSav.size()
                << "\n";
      if (!firstDiffs.empty()) {
        std::cout << "first diffs:";
        for (size_t off : firstDiffs) {
          std::cout << " 0x" << std::hex << std::setw(4) << std::setfill('0')
                    << off << ":" << std::setw(2) << (int)refSav[off] << "->"
                    << std::setw(2) << (int)outSav[off] << std::dec;
        }
        std::cout << "\n";
      }
    } else {
      std::cout << "diff bytes=unknown (size mismatch ref=" << refSav.size()
                << " out=" << outSav.size() << ")\n";
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
