#include "gui/MainWindow.h"

#include "emulator/gba/GBA.h"
#include "emulator/ps1/PS1.h"
#include "emulator/switch/SwitchEmulator.h"
#include "emulator/windows/WindowsEmulator.h"

#include "emulator/switch/GpuCore.h"

#include "emulator/common/Logger.h"
#include "emulator/ps1/PS1Constants.h"

#include "input/InputManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPixmap>
#include <QStringList>
#include <QTimer>

#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>

#include "common/PixelScaler.h"

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace AIO {
namespace GUI {

namespace {

std::string ReplaceExtension(const std::string &path,
                             const std::string &suffix) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos)
    return path + suffix;
  return path.substr(0, dot) + suffix + path.substr(dot);
}

bool WriteRgb555Ppm(const std::string &path, const uint16_t *pixels,
                    uint32_t width, uint32_t height, uint32_t stride,
                    uint32_t startX, uint32_t startY,
                    double *outNonBlackRatio = nullptr) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }

  out << "P6\n" << width << " " << height << "\n255\n";

  uint64_t nonBlack = 0;
  const uint64_t total = static_cast<uint64_t>(width) * height;
  for (uint32_t y = 0; y < height; ++y) {
    const uint16_t *row = pixels + (startY + y) * stride + startX;
    for (uint32_t x = 0; x < width; ++x) {
      const uint16_t px = row[x];
      const uint8_t r = static_cast<uint8_t>((px & 0x1F) << 3);
      const uint8_t g = static_cast<uint8_t>(((px >> 5) & 0x1F) << 3);
      const uint8_t b = static_cast<uint8_t>(((px >> 10) & 0x1F) << 3);
      if ((r | g | b) != 0) {
        ++nonBlack;
      }
      out.put(static_cast<char>(r));
      out.put(static_cast<char>(g));
      out.put(static_cast<char>(b));
    }
  }

  if (outNonBlackRatio) {
    *outNonBlackRatio = total > 0 ? static_cast<double>(nonBlack) / total : 0.0;
  }
  return true;
}

} // namespace

bool MainWindow::DumpCurrentFramePPM(const std::string &path,
                                     double *outNonBlackRatio) const {
  if (displayImage.isNull() || displayImage.width() <= 0 ||
      displayImage.height() <= 0) {
    AIO::Emulator::Common::Logger::Instance().Log(
        AIO::Emulator::Common::LogLevel::Warning, "MainWindow",
        "DumpCurrentFramePPM: displayImage is empty");
    if (outNonBlackRatio) {
      *outNonBlackRatio = 0.0;
    }
    return false;
  }

  QImage img = displayImage;
  if (img.format() != QImage::Format_ARGB32) {
    img = img.convertToFormat(QImage::Format_ARGB32);
  }

  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    AIO::Emulator::Common::Logger::Instance().LogFmt(
        AIO::Emulator::Common::LogLevel::Error, "MainWindow",
        "DumpCurrentFramePPM: failed to open '%s'", path.c_str());
    if (outNonBlackRatio) {
      *outNonBlackRatio = 0.0;
    }
    return false;
  }

  const int w = img.width();
  const int h = img.height();
  out << "P6\n" << w << " " << h << "\n255\n";

  uint64_t nonBlack = 0;
  const uint64_t total = (uint64_t)w * (uint64_t)h;

  for (int y = 0; y < h; ++y) {
    const uint32_t *row =
        reinterpret_cast<const uint32_t *>(img.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const uint32_t px = row[x];
      const uint8_t r = (uint8_t)((px >> 16) & 0xFF);
      const uint8_t g = (uint8_t)((px >> 8) & 0xFF);
      const uint8_t b = (uint8_t)((px >> 0) & 0xFF);
      if ((r | g | b) != 0) {
        ++nonBlack;
      }
      out.put((char)r);
      out.put((char)g);
      out.put((char)b);
    }
  }

  const double ratio = (total > 0) ? ((double)nonBlack / (double)total) : 0.0;
  if (outNonBlackRatio) {
    *outNonBlackRatio = ratio;
  }

  AIO::Emulator::Common::Logger::Instance().LogFmt(
      AIO::Emulator::Common::LogLevel::Info, "MainWindow",
      "DumpCurrentFramePPM: wrote %dx%d PPM to '%s' (nonBlackRatio=%.6f)", w, h,
      path.c_str(), ratio);

  if (currentEmulator == EmulatorType::PS1 && ps1Emulator) {
    const uint16_t *vram = ps1Emulator->GetGPU().GetVRAMPointer();
    const uint32_t stride = ps1Emulator->GetVRAMStride();
    const uint32_t bufferWidth = 512;
    const uint32_t bufferHeight =
        std::min<uint32_t>(ps1Emulator->GetDisplayHeight(), 240);

    double bufARatio = 0.0;
    double bufBRatio = 0.0;
    const std::string bufAPath = ReplaceExtension(path, "_bufA");
    const std::string bufBPath = ReplaceExtension(path, "_bufB");

    const bool wroteA = WriteRgb555Ppm(bufAPath, vram, bufferWidth,
                                       bufferHeight, stride, 0, 0, &bufARatio);
    const bool wroteB = WriteRgb555Ppm(
        bufBPath, vram, bufferWidth, bufferHeight, stride, 512, 0, &bufBRatio);

    AIO::Emulator::Common::Logger::Instance().LogFmt(
        AIO::Emulator::Common::LogLevel::Info, "MainWindow",
        "DumpCurrentFramePPM: PS1 buffer sidecars A=%s (ratio=%.6f) B=%s "
        "(ratio=%.6f)",
        wroteA ? bufAPath.c_str() : "<failed>", bufARatio,
        wroteB ? bufBPath.c_str() : "<failed>", bufBRatio);
  }

  return true;
}

uint64_t MainWindow::GetEmulatedMilliseconds() const {
  switch (currentEmulator) {
  case EmulatorType::GBA:
    if (gba) {
      constexpr uint64_t kGbaCpuHz = 16777216ULL;
      return (gba->GetTotalCycles() * 1000ULL) / kGbaCpuHz;
    }
    break;
  case EmulatorType::PS1:
    if (ps1Emulator) {
      constexpr uint64_t kPs1CpuHz = 33868800ULL;
      return (ps1Emulator->GetTotalCycles() * 1000ULL) / kPs1CpuHz;
    }
    break;
  case EmulatorType::Switch:
  case EmulatorType::Windows:
  case EmulatorType::None:
    break;
  }

  return 0;
}

static uint16_t ScriptKeyMaskFromName(const std::string &name) {
  // GBA KEYINPUT bits (0 = pressed): 0:A 1:B 2:Select 3:Start 4:Right 5:Left
  // 6:Up 7:Down 8:R 9:L
  static const std::unordered_map<std::string, uint16_t> k = {
      {"A", 1u << 0},     {"B", 1u << 1},     {"SELECT", 1u << 2},
      {"START", 1u << 3}, {"RIGHT", 1u << 4}, {"LEFT", 1u << 5},
      {"UP", 1u << 6},    {"DOWN", 1u << 7},  {"R", 1u << 8},
      {"L", 1u << 9},
  };
  auto it = k.find(name);
  return (it == k.end()) ? 0 : it->second;
}

static const char *ScriptNameFromMask(uint16_t mask) {
  switch (mask) {
  case (1u << 0):
    return "A";
  case (1u << 1):
    return "B";
  case (1u << 2):
    return "SELECT";
  case (1u << 3):
    return "START";
  case (1u << 4):
    return "RIGHT";
  case (1u << 5):
    return "LEFT";
  case (1u << 6):
    return "UP";
  case (1u << 7):
    return "DOWN";
  case (1u << 8):
    return "R";
  case (1u << 9):
    return "L";
  default:
    return "?";
  }
}

static std::optional<std::vector<MainWindow::ScriptEvent>>
LoadInputScriptMs(const QString &path) {
  std::ifstream f(path.toStdString());
  if (!f.is_open()) {
    return std::nullopt;
  }

  std::vector<MainWindow::ScriptEvent> events;
  std::string line;
  int lineNo = 0;
  while (std::getline(f, line)) {
    lineNo++;
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

    for (char &c : key)
      c = (char)std::toupper((unsigned char)c);
    for (char &c : action)
      c = (char)std::toupper((unsigned char)c);

    const uint16_t mask = ScriptKeyMaskFromName(key);
    if (mask == 0) {
      std::cout << "[SCRIPT] unknown key '" << key << "' at line " << lineNo
                << std::endl;
      continue;
    }

    const bool down =
        (action == "DOWN" || action == "PRESS" || action == "PRESSED");
    const bool up =
        (action == "UP" || action == "RELEASE" || action == "RELEASED");
    if (!down && !up) {
      std::cout << "[SCRIPT] unknown action '" << action << "' at line "
                << lineNo << std::endl;
      continue;
    }

    events.push_back(MainWindow::ScriptEvent{(int64_t)ms, mask, down});
  }

  std::sort(events.begin(), events.end(), [](const auto &a, const auto &b) {
    if (a.ms != b.ms)
      return a.ms < b.ms;
    // Apply DOWN before UP when timestamps collide.
    return (int)a.down > (int)b.down;
  });

  return events;
}

void MainWindow::toggleDevPanel(bool enabled) {
  devPanelLabel->setVisible(enabled);
  if (enabled) {
    resize(480, 530);
  } else {
    resize(480, 450);
  }
}

void MainWindow::EnableDebugger(bool enabled) {
  debuggerEnabled = enabled;
  if (enabled) {
    gba->SetSingleStep(true);
    // Enable terminal raw mode for arrow/enter handling
    struct termios tio;
    if (tcgetattr(STDIN_FILENO, &tio) == 0) {
      rawTermios = tio;
      tio.c_lflag &= ~(ICANON | ECHO);
      tio.c_cc[VMIN] = 0;
      tio.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSANOW, &tio);
      stdinRawEnabled = true;
    }
  } else {
    gba->SetSingleStep(false);
    if (stdinRawEnabled) {
      tcsetattr(STDIN_FILENO, TCSANOW, &rawTermios);
      stdinRawEnabled = false;
    }
  }
}

void MainWindow::AddBreakpoint(uint32_t addr) { gba->AddBreakpoint(addr); }

QString MainWindow::formatInputState(uint16_t state) {
  // GBA KEYINPUT: 0 = pressed, 1 = released
  // Bits: 0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L
  QStringList pressed;
  if (!(state & 0x001))
    pressed << "A";
  if (!(state & 0x002))
    pressed << "B";
  if (!(state & 0x004))
    pressed << "SEL";
  if (!(state & 0x008))
    pressed << "START";
  if (!(state & 0x010))
    pressed << "→";
  if (!(state & 0x020))
    pressed << "←";
  if (!(state & 0x040))
    pressed << "↑";
  if (!(state & 0x080))
    pressed << "↓";
  if (!(state & 0x100))
    pressed << "R";
  if (!(state & 0x200))
    pressed << "L";

  if (pressed.isEmpty()) {
    return "None";
  }
  return pressed.join(" + ");
}

void MainWindow::LoadROM(const std::string &path) {
  bool success = false;

  if (currentEmulator == EmulatorType::GBA) {
    success = gba->LoadROM(path);
    if (success) {
      // GBA Resolution
      displayImage = QImage(240, 160, QImage::Format_ARGB32);

      // Allow the emulator viewport to expand/shrink with the window.
      // (A previous fixed-size here prevented resizing and caused clipping.)
      if (displayLabel) {
        displayLabel->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
        displayLabel->setMinimumSize(0, 0);
        displayLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      }
    }
  } else if (currentEmulator == EmulatorType::PS1) {
    // Try loading a real BIOS if one is configured
    std::string biosPath;
    const QString settingsBios = settings.value("ps1/biosPath").toString();
    if (!settingsBios.isEmpty()) {
      biosPath = settingsBios.toStdString();
    } else if (const char *envBios = std::getenv("AIO_PS1_BIOS")) {
      biosPath = envBios;
    }

    if (!biosPath.empty()) {
      ps1Emulator->LoadBIOS(biosPath);
    }

    success = ps1Emulator->LoadDisc(path);

    // If no real BIOS was loaded, boot via HLE (parse EXE from disc)
    if (success && !ps1Emulator->IsBIOSLoaded()) {
      if (!ps1Emulator->InitHLE()) {
        statusLabel->setText("Failed to initialize PS1 HLE BIOS from disc.");
        return;
      }
    }
    if (success) {
      uint32_t w = ps1Emulator->GetDisplayWidth();
      uint32_t h = ps1Emulator->GetDisplayHeight();
      if (w == 0)
        w = 320;
      if (h == 0)
        h = 240;
      displayImage = QImage(w, h, QImage::Format_ARGB32);

      if (displayLabel) {
        displayLabel->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
        displayLabel->setMinimumSize(0, 0);
        displayLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      }
    }
  } else if (currentEmulator == EmulatorType::Switch) {
    success = switchEmulator->LoadROM(path);
    if (success) {
      // Switch Resolution (720p)
      // Scale down for display if needed, or show smaller window
      displayImage = QImage(1280, 720, QImage::Format_ARGB32);

      // Allow the emulator viewport to expand/shrink with the window.
      if (displayLabel) {
        displayLabel->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
        displayLabel->setMinimumSize(0, 0);
        displayLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      }
    }
  } else if (currentEmulator == EmulatorType::Windows) {
    success = windowsEmulator->LoadROM(path);
    if (success) {
      displayImage = QImage(1280, 720, QImage::Format_ARGB32);
      if (displayLabel) {
        displayLabel->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
        displayLabel->setMinimumSize(0, 0);
        displayLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      }
    }
  }

  if (success) {
    statusLabel->setText("ROM Loaded: " + QString::fromStdString(path));

    // Optional scripted input playback (debugging aid).
    inputScript_.clear();
    nextScriptEvent_ = 0;
    scriptKeyState_ = 0x03FF;
    scriptEnabled_.store(false, std::memory_order_relaxed);

    if (!inputScriptPath_.isEmpty()) {
      auto loaded = LoadInputScriptMs(inputScriptPath_);
      if (loaded) {
        inputScript_ = std::move(*loaded);
        scriptEnabled_.store(!inputScript_.empty(), std::memory_order_relaxed);
        scriptTimer_.restart();
        std::cout << "[SCRIPT] loaded " << inputScript_.size()
                  << " events from " << inputScriptPath_.toStdString()
                  << std::endl;
      } else {
        std::cout << "[SCRIPT] failed to open script: "
                  << inputScriptPath_.toStdString() << std::endl;
      }
    }

    // Publish a fresh KEYINPUT snapshot before starting emulation.
    // LoadROM currently starts the emulation thread before the navigation timer
    // has a chance to poll again; without this, the core can see stale UI input
    // (e.g., Down held during menu navigation) for the first few frames.
    AIO::Input::InputManager::instance().setActiveContext(
        AIO::Input::InputContext::Emulator);
    // First force release-all, then take a fresh synchronous poll.
    // This avoids "level starts crouching" if Down was held for UI navigation
    // or if the controller reports a biased axis during connect.
    pendingEmuKeyinput.store(0x03FF, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto snapshot = AIO::Input::InputManager::instance().pollNow();
    const uint16_t desiredKeyinput =
        (scriptEnabled_.load(std::memory_order_relaxed) ? scriptKeyState_
                                                        : snapshot.keyinput);
    pendingEmuKeyinput.store(desiredKeyinput, std::memory_order_relaxed);

    // Start emulator thread and display update timer
    StartEmulatorThread();
    displayTimer->start(16); // ~60 Hz display updates

    // Switch to emulator view
    stackedWidget->setCurrentWidget(emulatorPage);

    // Ensure keyboard focus for input
    setFocus();
    activateWindow();
  } else {
    statusLabel->setText("Failed to load ROM");
  }
}

void MainWindow::SetInputScriptPath(const std::string &path) {
  inputScriptPath_ = QString::fromStdString(path);
}

void MainWindow::SetEmulatorType(int type) {
  if (type == 0) {
    currentEmulator = EmulatorType::GBA;
    std::cout << "[MainWindow] Set emulator type to GBA" << std::endl;
  } else if (type == 1) {
    currentEmulator = EmulatorType::Switch;
    std::cout << "[MainWindow] Set emulator type to Switch" << std::endl;
  } else if (type == 2) {
    currentEmulator = EmulatorType::PS1;
    std::cout << "[MainWindow] Set emulator type to PS1" << std::endl;
  } else if (type == 3) {
    currentEmulator = EmulatorType::Windows;
    std::cout << "[MainWindow] Set emulator type to Windows" << std::endl;
  }
}

void MainWindow::StartEmulatorThread() {
  if (emulatorRunning.exchange(true)) {
    return; // Already running
  }

  // Lazily initialize audio on first emulator run to avoid blocking app
  // startup.
  if (audioDevice == 0 && (currentEmulator == EmulatorType::GBA ||
                           currentEmulator == EmulatorType::PS1)) {
    initAudio();
  }

  emulatorThread = std::thread(&MainWindow::EmulatorThreadMain, this);

  // Start audio immediately when emulation begins.
  // The APU ring buffer already returns silence on underrun, so delaying audio
  // start just creates an artificial "startup silence".
  if (audioDevice != 0 && (currentEmulator == EmulatorType::GBA ||
                           currentEmulator == EmulatorType::PS1)) {
    SDL_PauseAudioDevice(audioDevice, 0);
  }
}

void MainWindow::StopEmulatorThread() {
  if (audioDevice != 0) {
    SDL_PauseAudioDevice(audioDevice, 1);
  }
  emulatorRunning = false;
  if (emulatorThread.joinable()) {
    emulatorThread.join();
  }

  if (currentEmulator == EmulatorType::GBA) {
    gba->GetMemory().FlushSave();
  }
}

void MainWindow::EmulatorThreadMain() {
  // Emulator loop runs on background thread
  // Executes CPU cycles independent of Qt event processing
  using Clock = std::chrono::steady_clock;

  // GBA timing: 228 scanlines per frame * 1232 cycles/scanline.
  static constexpr int kGbaCyclesPerFrame = 1232 * 228; // 280,896
  static constexpr double kGbaCpuHz = 16777216.0;       // 16.777216 MHz
  const double nativeFps = kGbaCpuHz / (double)kGbaCyclesPerFrame;

  double targetFps = nativeFps;
  if (const char *v = std::getenv("AIO_GBA_TARGET_FPS")) {
    const double parsed = std::atof(v);
    if (parsed >= 1.0 && parsed <= 240.0) {
      targetFps = parsed;
    }
  }

  const auto gbaFrameDuration = std::chrono::duration<double>(1.0 / targetFps);

  // Use a deadline-based scheduler so occasional sleep overshoot doesn't
  // permanently slow emulation.
  Clock::time_point nextFrame = Clock::now();

  uint16_t lastAppliedKeyinput = 0x03FF;
  auto applyPendingKeyinput = [&]() {
    if (currentEmulator != EmulatorType::GBA) {
      return;
    }
    const uint16_t desired =
        scriptEnabled_.load(std::memory_order_relaxed)
            ? pendingEmuKeyinput.load(std::memory_order_relaxed)
            : AIO::Input::InputManager::instance().snapshot().keyinput;
    if (desired != lastAppliedKeyinput) {
      gba->UpdateInput(desired);
      lastAppliedKeyinput = desired;
    }
  };

  while (emulatorRunning) {
    // Handle step-back request FIRST (before pause check)
    int stepBackCount = emulatorStepBack.exchange(0, std::memory_order_relaxed);
    if (stepBackCount > 0) {
      if (frameHistoryWritten > 0 && currentEmulator == EmulatorType::GBA) {
        // Walk back to the most recently written slot
        size_t snapshotIdx =
            (frameHistoryIndex + MAX_FRAME_HISTORY - 1) % MAX_FRAME_HISTORY;

        const auto &snap = (*frameHistory)[snapshotIdx];

        auto &mem = gba->GetMemory();
        std::memcpy(mem.GetIWRAM(), snap.iwram.data(),
                    FrameSnapshot::IWRAM_SIZE);
        std::memcpy(mem.GetEWRAM(), snap.ewram.data(),
                    FrameSnapshot::EWRAM_SIZE);
        std::memcpy(mem.GetVRAM(), snap.vram.data(), FrameSnapshot::VRAM_SIZE);
        std::memcpy(mem.GetOAM(), snap.oam.data(), FrameSnapshot::OAM_SIZE);
        std::memcpy(mem.GetPaletteRAM(), snap.palette.data(),
                    FrameSnapshot::PALETTE_SIZE);
        std::memcpy(mem.GetIORegs(), snap.ioRegs.data(),
                    FrameSnapshot::IO_SIZE);

        for (int i = 0; i < 16; i++) {
          gba->SetRegister(i, snap.cpuRegisters[i]);
        }

        gba->GetPPU().RestoreFramebuffer(snap.framebuffer.data(),
                                         FrameSnapshot::FB_SIZE);

        emulatorFrameNumber.store(snap.frameNum, std::memory_order_relaxed);

        // Rewind the ring index
        frameHistoryIndex = snapshotIdx;
        if (frameHistoryWritten > 0)
          frameHistoryWritten--;
      }

      nextFrame = Clock::now();
      continue;
    }

    if (emulatorPaused && !emulatorStepOne.load()) {
      nextFrame = Clock::now();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    // Execute one frame
    if (currentEmulator == EmulatorType::GBA) {
      static int cycleCarry = 0;
      if (emulatorFrameNumber.load(std::memory_order_relaxed) == 0)
        cycleCarry = 0;
      int totalCycles = cycleCarry;

      // Run the frame in smaller chunks to reduce worst-case input latency.
      // At 60fps, 64 chunks is ~0.26ms granularity for when pending KEYINPUT is
      // applied.
      int chunksPerFrame = 64;
      if (const char *v = std::getenv("AIO_GBA_INPUT_CHUNKS")) {
        const int parsed = std::atoi(v);
        if (parsed >= 4 && parsed <= 256) {
          chunksPerFrame = parsed;
        }
      }
      const int chunkCyclesTarget = kGbaCyclesPerFrame / chunksPerFrame;

      for (int chunk = 0; chunk < chunksPerFrame && emulatorRunning; ++chunk) {
        applyPendingKeyinput();

        int chunkCycles = 0;
        while (chunkCycles < chunkCyclesTarget &&
               totalCycles < kGbaCyclesPerFrame && emulatorRunning) {
          const int stepCycles = gba->Step();
          chunkCycles += stepCycles;
          totalCycles += stepCycles;
        }
      }

      // Catch any remainder cycles due to integer division.
      while (totalCycles < kGbaCyclesPerFrame && emulatorRunning) {
        totalCycles += gba->Step();
      }

      // Carry over excess cycles to the next frame for accurate timing
      cycleCarry = totalCycles - kGbaCyclesPerFrame;

      applyPendingKeyinput();

      // Periodically flush save
      saveFlushCounter++;
      if (saveFlushCounter >= SAVE_FLUSH_INTERVAL) {
        saveFlushCounter = 0;
        gba->GetMemory().FlushSave();
      }
    } else if (currentEmulator == EmulatorType::PS1) {
      // PS1: 33.8688 MHz CPU, 263 scanlines × 2171 cycles/scanline ≈ 570,973
      // cycles/frame
      static constexpr int kPs1CyclesPerFrame = 2171 * 263;
      static int ps1CycleCarry = 0;
      if (emulatorFrameNumber.load(std::memory_order_relaxed) == 0)
        ps1CycleCarry = 0;
      int totalCycles = ps1CycleCarry;

      // Map logical input state directly to PS1 pad buttons (both active-low).
      // Uses the logical snapshot — not the GBA keyinput proxy — so all
      // buttons including Square/Triangle/Aux are correctly propagated.
      const auto inputSnap = AIO::Input::InputManager::instance().snapshot();
      const uint32_t logical = inputSnap.logical;

      auto logPressed = [&](AIO::Input::LogicalButton b) -> bool {
        const uint32_t m = 1u << static_cast<uint32_t>(b);
        return (logical & m) == 0; // 0 = pressed (active-low)
      };

      uint16_t ps1Buttons = 0xFFFF; // All released
      if (logPressed(AIO::Input::LogicalButton::Confirm))
        ps1Buttons &= ~Emulator::PS1::PadButton::Cross;
      if (logPressed(AIO::Input::LogicalButton::Back))
        ps1Buttons &= ~Emulator::PS1::PadButton::Circle;
      if (logPressed(AIO::Input::LogicalButton::Aux1))
        ps1Buttons &= ~Emulator::PS1::PadButton::Square;
      if (logPressed(AIO::Input::LogicalButton::Aux2))
        ps1Buttons &= ~Emulator::PS1::PadButton::Triangle;
      if (logPressed(AIO::Input::LogicalButton::Select))
        ps1Buttons &= ~Emulator::PS1::PadButton::Select;
      if (logPressed(AIO::Input::LogicalButton::Start))
        ps1Buttons &= ~Emulator::PS1::PadButton::Start;
      if (logPressed(AIO::Input::LogicalButton::L))
        ps1Buttons &= ~Emulator::PS1::PadButton::L1;
      if (logPressed(AIO::Input::LogicalButton::R))
        ps1Buttons &= ~Emulator::PS1::PadButton::R1;
      if (logPressed(AIO::Input::LogicalButton::Up))
        ps1Buttons &= ~Emulator::PS1::PadButton::Up;
      if (logPressed(AIO::Input::LogicalButton::Down))
        ps1Buttons &= ~Emulator::PS1::PadButton::Down;
      if (logPressed(AIO::Input::LogicalButton::Left))
        ps1Buttons &= ~Emulator::PS1::PadButton::Left;
      if (logPressed(AIO::Input::LogicalButton::Right))
        ps1Buttons &= ~Emulator::PS1::PadButton::Right;
      ps1Emulator->UpdateInput(ps1Buttons);

      while (totalCycles < kPs1CyclesPerFrame && emulatorRunning) {
        totalCycles += ps1Emulator->Step();
      }
      ps1CycleCarry = totalCycles - kPs1CyclesPerFrame;
    } else if (currentEmulator == EmulatorType::Switch) {
      switchEmulator->RunFrame();
    } else if (currentEmulator == EmulatorType::Windows) {
      windowsEmulator->RunFrame();
    }

    // Always save frame snapshot for step-back capability (BEFORE incrementing
    // counter)
    if (currentEmulator == EmulatorType::GBA) {
      auto &snap = (*frameHistory)[frameHistoryIndex];
      snap.frameNum = emulatorFrameNumber.load();

      const auto &mem = gba->GetMemory();
      std::memcpy(snap.iwram.data(), mem.GetIWRAM(), FrameSnapshot::IWRAM_SIZE);
      std::memcpy(snap.ewram.data(), mem.GetEWRAM(), FrameSnapshot::EWRAM_SIZE);
      std::memcpy(snap.vram.data(), mem.GetVRAMData(),
                  FrameSnapshot::VRAM_SIZE);
      std::memcpy(snap.oam.data(), mem.GetOAMData(), FrameSnapshot::OAM_SIZE);
      std::memcpy(snap.palette.data(), mem.GetPaletteData(),
                  FrameSnapshot::PALETTE_SIZE);
      std::memcpy(snap.ioRegs.data(), mem.GetIORegs(), FrameSnapshot::IO_SIZE);

      gba->GetPPU().CopyFramebufferTo(snap.framebuffer.data(),
                                      FrameSnapshot::FB_SIZE);

      for (int i = 0; i < 16; i++) {
        snap.cpuRegisters[i] = gba->GetRegister(i);
      }
      snap.cpsr = gba->GetCPSR();

      frameHistoryWritten =
          std::min(frameHistoryWritten + 1, MAX_FRAME_HISTORY);
      frameHistoryIndex = (frameHistoryIndex + 1) % MAX_FRAME_HISTORY;
    }

    // Increment frame counter
    emulatorFrameNumber.fetch_add(1, std::memory_order_relaxed);

    // If step-one was requested, pause after this frame
    if (emulatorStepOne.exchange(false, std::memory_order_relaxed)) {
      emulatorPaused.store(true, std::memory_order_relaxed);
    }

    // Advance deadline (pick duration based on active emulator).
    const auto frameDur =
        (currentEmulator == EmulatorType::GBA)
            ? std::chrono::duration_cast<Clock::duration>(gbaFrameDuration)
        : (currentEmulator == EmulatorType::PS1)
            ? std::chrono::duration_cast<Clock::duration>(
                  std::chrono::duration<double>(1.0 / 59.94))
            : std::chrono::milliseconds(16);

    // Maintain an absolute "next frame" deadline so we self-correct after
    // oversleep.
    nextFrame += frameDur;

    // If we're far behind (e.g., breakpoint / scheduling hiccup), drop
    // accumulated lag.
    const auto now = Clock::now();
    if (now > nextFrame + frameDur * 4) {
      nextFrame = now;
    }

    // Audio-sync frame pacing: the audio callback consumes samples at a
    // fixed real-time rate, so the ring buffer fill level naturally tells
    // us whether we're ahead or behind. We simply run frames at the
    // deadline rate and let the ring buffer absorb jitter. If we fall
    // very far behind (> 4 frames), we snap the deadline forward to
    // avoid a burst of catch-up frames.
    if (now < nextFrame) {
      std::this_thread::sleep_until(nextFrame);
    }
  }
}

void MainWindow::UpdateDisplay() {
  // UI timer callback: update display from emulator state
  // Runs on main Qt thread at 60 Hz

  // Input polling is owned by the navigation timer; this UI tick must remain
  // read-only to avoid fighting over InputManager state.
  const auto snapshot = AIO::Input::InputManager::instance().snapshot();

  uint16_t inputState = snapshot.keyinput;

  // Route input based on the active UI page.
  QWidget *current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
  const bool inEmu = (current == emulatorPage) && emulatorRunning;

  // Two-layer input model:
  // - Our Application (menus/booter): driven by navTimer +
  // NavigationController/UIActionMapper.
  // - Sub-applications (emulator runtime, streaming/web apps): may handle keys
  // directly. Important: do NOT drive menu navigation here as well, or we'll
  // double-dispatch actions.
  const bool isSubAppPage =
      (current == emulatorPage) || (current == streamingWebPage) ||
      (current == youTubeBrowsePage) || (current == youTubePlayerPage);

  const bool inStreamingUi = (current == streamingWebPage) ||
                             (current == youTubeBrowsePage) ||
                             (current == youTubePlayerPage);

  // Sub-app layer: synthesize basic keys for pages that rely on keyPressEvent.
  // Note: emulator runtime itself is fed via gba->UpdateInput below.
  if (isSubAppPage && current != emulatorPage) {
    // Fallback: synthesize key presses so existing keyPressEvent handlers work.
    // Includes repeat for held directions to make controller navigation
    // consistent.
    QWidget *target = QApplication::focusWidget();
    if (!target)
      target = current ? current : this;
    if (target && target->focusProxy())
      target = target->focusProxy();

    auto sendKey = [&](int qtKey) {
      QKeyEvent ev(QEvent::KeyPress, qtKey, Qt::NoModifier);
      QCoreApplication::sendEvent(target, &ev);
    };

    auto logicalPressed = [&](AIO::Input::LogicalButton b) {
      const uint32_t mask = 1u << static_cast<uint32_t>(b);
      return (snapshot.logical & mask) == 0;
    };

    // Persistent UI controller state for repeat handling.
    struct RepeatState {
      bool down = false;
      qint64 nextMs = 0;
    };
    static RepeatState repLeft, repRight, repUp, repDown;
    static QElapsedTimer uiRepeatTimer;
    if (!uiRepeatTimer.isValid())
      uiRepeatTimer.start();
    const qint64 nowMs = uiRepeatTimer.elapsed();

    constexpr qint64 INITIAL_DELAY_MS = 220;
    constexpr qint64 REPEAT_MS = 70;

    auto handleRepeatLogical = [&](AIO::Input::LogicalButton logical, int qtKey,
                                   RepeatState &st, uint32_t &lastLogical) {
      const uint32_t mask = 1u << static_cast<uint32_t>(logical);
      const bool isDown = logicalPressed(logical);
      const bool wasDown = (lastLogical & mask) == 0;

      if (isDown && !wasDown) {
        // Initial press
        st.down = true;
        st.nextMs = nowMs + INITIAL_DELAY_MS;
        sendKey(qtKey);
        lastLogical &= ~mask;
        return;
      }

      if (isDown && wasDown) {
        // Held
        if (st.down && nowMs >= st.nextMs) {
          sendKey(qtKey);
          st.nextMs = nowMs + REPEAT_MS;
        }
        return;
      }

      // Released
      st.down = false;
      lastLogical |= mask;
    };

    static uint32_t lastLogicalUi = 0xFFFFFFFFu;
    handleRepeatLogical(AIO::Input::LogicalButton::Left, Qt::Key_Left, repLeft,
                        lastLogicalUi);
    handleRepeatLogical(AIO::Input::LogicalButton::Right, Qt::Key_Right,
                        repRight, lastLogicalUi);
    handleRepeatLogical(AIO::Input::LogicalButton::Up, Qt::Key_Up, repUp,
                        lastLogicalUi);
    handleRepeatLogical(AIO::Input::LogicalButton::Down, Qt::Key_Down, repDown,
                        lastLogicalUi);

    // Buttons (edge-triggered)
    auto edgeLogical = [&](AIO::Input::LogicalButton logical) {
      const uint32_t mask = 1u << static_cast<uint32_t>(logical);
      const bool isDown = logicalPressed(logical);
      const bool wasDown = (lastLogicalUi & mask) == 0;
      return isDown && !wasDown;
    };

    auto handleEdgeLogical = [&](AIO::Input::LogicalButton logical, int qtKey) {
      const uint32_t mask = 1u << static_cast<uint32_t>(logical);
      const bool isDown = logicalPressed(logical);
      const bool wasDown = (lastLogicalUi & mask) == 0;
      if (isDown && !wasDown) {
        sendKey(qtKey);
        lastLogicalUi &= ~mask;
      } else if (!isDown && wasDown) {
        lastLogicalUi |= mask;
      }
    };

    handleEdgeLogical(AIO::Input::LogicalButton::Confirm, Qt::Key_Return);
    handleEdgeLogical(AIO::Input::LogicalButton::Back, Qt::Key_Escape);
  }

  if (currentEmulator == EmulatorType::GBA) {
    if (inEmu && scriptEnabled_.load(std::memory_order_relaxed) &&
        scriptTimer_.isValid()) {
      int64_t nowMs = 0;
      const QString timebase =
          qEnvironmentVariable("AIO_INPUT_SCRIPT_TIMEBASE").trimmed().toUpper();
      if (timebase == "EMU") {
        constexpr uint64_t CYCLES_PER_SECOND = 16780000ULL;
        nowMs =
            (int64_t)((gba->GetTotalCycles() * 1000ULL) / CYCLES_PER_SECOND);
      } else {
        nowMs = (int64_t)scriptTimer_.elapsed();
      }
      while (nextScriptEvent_ < inputScript_.size() &&
             inputScript_[nextScriptEvent_].ms <= nowMs) {
        const auto &ev = inputScript_[nextScriptEvent_];
        if (ev.down) {
          scriptKeyState_ = (uint16_t)(scriptKeyState_ & ~ev.mask);
        } else {
          scriptKeyState_ = (uint16_t)(scriptKeyState_ | ev.mask);
        }

        const uint16_t dispcnt = gba->ReadMem16(0x04000000);
        const uint16_t winin = gba->ReadMem16(0x04000048);
        const uint16_t winout = gba->ReadMem16(0x0400004A);
        const uint16_t bldcnt = gba->ReadMem16(0x04000050);
        const uint16_t bldalpha = gba->ReadMem16(0x04000052);
        const uint16_t win0h = gba->ReadMem16(0x04000040);
        const uint16_t win0v = gba->ReadMem16(0x04000044);
        std::cout << "[SCRIPT] t_ms=" << nowMs << " event_ms=" << ev.ms
                  << " key=" << ScriptNameFromMask(ev.mask)
                  << " action=" << (ev.down ? "DOWN" : "UP") << " keyState=0x"
                  << std::hex << scriptKeyState_ << std::dec << " pc=0x"
                  << std::hex << gba->GetPC() << std::dec << " DISPCNT=0x"
                  << std::hex << dispcnt << " WININ=0x" << winin << " WINOUT=0x"
                  << winout << " WIN0H=0x" << win0h << " WIN0V=0x" << win0v
                  << " BLDCNT=0x" << bldcnt << " BLDALPHA=0x" << bldalpha
                  << std::dec << std::endl;
        nextScriptEvent_++;
      }
      inputState = scriptKeyState_;
    }

    // Publish the computed input state to the emulation thread.
    // The core input update is applied from EmulatorThreadMain() in small
    // chunks for low latency; UpdateDisplay() is where scripted input events
    // are advanced based on the chosen timebase.
    if (inEmu) {
      pendingEmuKeyinput.store(inputState, std::memory_order_relaxed);
    }

    // Copy framebuffer atomically under lock to prevent tearing from
    // SwapBuffers race
    constexpr size_t FB_PIXELS = 240 * 160;
    uint32_t localFb[FB_PIXELS];
    gba->GetPPU().CopyFramebufferTo(localFb, FB_PIXELS);
    for (int y = 0; y < 160; ++y) {
      memcpy(displayImage.scanLine(y), &localFb[y * 240],
             240 * sizeof(uint32_t));
    }
    avRecorder_.RecordVideoFrame(localFb);
  } else if (currentEmulator == EmulatorType::Switch) {
    auto *gpu = switchEmulator->GetGPU();
    if (gpu) {
      const auto &buffer = gpu->GetFramebuffer();
      if (buffer.size() >= 1280 * 720) {
        memcpy(displayImage.bits(), buffer.data(),
               buffer.size() * sizeof(uint32_t));
        // Record frame if A/V recording is active
        avRecorder_.RecordVideoFrame(buffer);
      }
    }
  } else if (currentEmulator == EmulatorType::PS1) {
    // PS1 framebuffer is RGB555 (uint16_t) — convert to ARGB32 for display
    const uint16_t *ps1Fb = ps1Emulator->GetFramebuffer();
    const auto &ps1Gpu = ps1Emulator->GetGPU();
    const uint32_t w = ps1Emulator->GetDisplayWidth();
    const uint32_t h = ps1Emulator->GetDisplayHeight();
    if (ps1Fb && w > 0 && h > 0) {
      // Resize display image if GPU resolution changed
      if (static_cast<uint32_t>(displayImage.width()) != w ||
          static_cast<uint32_t>(displayImage.height()) != h) {
        displayImage = QImage(w, h, QImage::Format_ARGB32);
      }
      auto *dst = reinterpret_cast<uint32_t *>(displayImage.bits());
      if (ps1Gpu.IsDisplay24Bit()) {
        const uint32_t stride = ps1Emulator->GetVRAMStride();
        const auto *vramBytes =
            reinterpret_cast<const uint8_t *>(ps1Gpu.GetVRAMPointer());
        const uint32_t strideBytes = stride * sizeof(uint16_t);
        const uint32_t startXBytes = ps1Gpu.GetDisplayStartX() * 2u;
        const uint32_t startY = ps1Gpu.GetDisplayStartY();

        for (uint32_t y = 0; y < h; ++y) {
          const uint32_t srcY =
              (startY + y) & (AIO::Emulator::PS1::GPU::VRAM_HEIGHT - 1);
          const uint8_t *srcRow = vramBytes + srcY * strideBytes;
          uint32_t *dstRow = dst + y * w;
          for (uint32_t x = 0; x < w; ++x) {
            const uint32_t byteIndex = (startXBytes + x * 3u) % strideBytes;
            const uint8_t r = srcRow[byteIndex];
            const uint8_t g = srcRow[(byteIndex + 1u) % strideBytes];
            const uint8_t b = srcRow[(byteIndex + 2u) % strideBytes];
            dstRow[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                        (static_cast<uint32_t>(g) << 8) | b;
          }
        }
      } else {
        const uint32_t stride = ps1Gpu.GetFramebufferStride();
        for (uint32_t y = 0; y < h; ++y) {
          const uint16_t *srcRow = ps1Fb + y * stride;
          uint32_t *dstRow = dst + y * w;
          for (uint32_t x = 0; x < w; ++x) {
            uint16_t px = srcRow[x];
            uint8_t r = static_cast<uint8_t>((px & 0x1F) << 3);
            uint8_t g = static_cast<uint8_t>(((px >> 5) & 0x1F) << 3);
            uint8_t b = static_cast<uint8_t>(((px >> 10) & 0x1F) << 3);
            dstRow[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
          }
        }
      }

      if (avRecorder_.IsRecording()) {
        const int recordWidth = avRecorder_.GetVideoWidth();
        const int recordHeight = avRecorder_.GetVideoHeight();
        if (recordWidth == static_cast<int>(w) &&
            recordHeight == static_cast<int>(h)) {
          avRecorder_.RecordVideoFrame(dst);
        } else if (recordWidth > 0 && recordHeight > 0) {
          std::vector<uint32_t> recordFrame(
              static_cast<size_t>(recordWidth) * recordHeight, 0xFF000000u);
          const uint32_t copyWidth =
              std::min<uint32_t>(w, static_cast<uint32_t>(recordWidth));
          const uint32_t copyHeight =
              std::min<uint32_t>(h, static_cast<uint32_t>(recordHeight));
          for (uint32_t row = 0; row < copyHeight; ++row) {
            std::memcpy(recordFrame.data() + row * recordWidth, dst + row * w,
                        copyWidth * sizeof(uint32_t));
          }
          avRecorder_.RecordVideoFrame(recordFrame);
        }
      }
    }
  } else if (currentEmulator == EmulatorType::Windows) {
    const QImage &fb = windowsEmulator->GetFramebuffer();
    if (!fb.isNull()) {
      displayImage = fb.copy();
    }
  }

  // Present to the UI with nearest-neighbor scaling.
  if (displayLabel && !displayImage.isNull()) {
    const auto rect = displayLabel->contentsRect();
    const int targetW = rect.width();
    const int targetH = rect.height();
    if (targetW > 0 && targetH > 0) {
      const int srcW = displayImage.width();
      const int srcH = displayImage.height();

      // PS1 (and other CRT-era consoles) always output a 4:3 signal to the TV
      // regardless of the GPU pixel resolution. The logical display size drives
      // the aspect-ratio-correct fit calculation, while scaling still operates
      // on the actual source pixel buffer.
      int logicalW = srcW;
      int logicalH = srcH;
      if (currentEmulator == EmulatorType::PS1 && srcH > 0) {
        logicalW = srcH * 4 / 3;
      }

      const auto mode = (videoScaleMode_ == VideoScaleMode::FitNearest)
                            ? AIO::Common::ScaleMode::FitNearest
                            : AIO::Common::ScaleMode::IntegerNearest;

      const auto scaled = AIO::Common::ComputeScaledSize(
          logicalW, logicalH, targetW, targetH, mode, videoIntegerScale_);

      if (scaled.width > 0 && scaled.height > 0) {
        if (scaledDisplayImage_.isNull() ||
            scaledDisplayImage_.width() != scaled.width ||
            scaledDisplayImage_.height() != scaled.height) {
          scaledDisplayImage_ =
              QImage(scaled.width, scaled.height, QImage::Format_ARGB32);
        }

        const auto *srcPixels =
            reinterpret_cast<const uint32_t *>(displayImage.constBits());
        const int srcStride = displayImage.bytesPerLine() / 4;
        auto *dstPixels =
            reinterpret_cast<uint32_t *>(scaledDisplayImage_.bits());
        const int dstStride = scaledDisplayImage_.bytesPerLine() / 4;

        // Use the fast integer path only when no aspect-ratio correction is
        // needed (source and logical dimensions match), otherwise fall back to
        // the general nearest-neighbor scaler which handles non-square pixels.
        if (mode == AIO::Common::ScaleMode::IntegerNearest &&
            scaled.integerScale > 0 &&
            scaled.width == srcW * scaled.integerScale &&
            scaled.height == srcH * scaled.integerScale) {
          AIO::Common::ScaleIntegerNearestARGB32(
              srcPixels, srcW, srcH, srcStride, dstPixels, scaled.integerScale,
              dstStride);
        } else {
          AIO::Common::ScaleNearestARGB32(srcPixels, srcW, srcH, srcStride,
                                          dstPixels, scaled.width,
                                          scaled.height, dstStride);
        }

        displayLabel->setPixmap(QPixmap::fromImage(scaledDisplayImage_));
      }
    }
  }

  // FPS calculation
  frameCount++;
  qint64 elapsed = fpsTimer.elapsed();
  if (elapsed >= 1000) {
    currentFPS = (frameCount * 1000.0) / elapsed;
    frameCount = 0;
    fpsTimer.restart();
  }

  // Update dev panel if visible
  if (devPanelLabel->isVisible()) {
    ::std::stringstream ss;
    ss << "<b>Frame:</b> " << emulatorFrameNumber.load() << "<br>";
    ss << "<b>FPS:</b> " << ::std::fixed << ::std::setprecision(1) << currentFPS
       << "<br>";
    ss << "<b>Status:</b> " << (emulatorPaused.load() ? "PAUSED" : "RUNNING")
       << "<br>";

    if (currentEmulator == EmulatorType::GBA) {
      uint16_t gameKeyInput = gba->ReadMem16(0x04000130);
      ss << "<b>PC:</b> 0x" << ::std::hex << ::std::setfill('0')
         << ::std::setw(8) << gba->GetPC() << "<br>";
      ss << "<b>Input:</b> " << formatInputState(inputState).toStdString()
         << "<br>";
      ss << "<b>KEYINPUT:</b> 0x" << ::std::hex << ::std::setw(4)
         << gameKeyInput << "<br>";
      ss << "<b>VCount:</b> " << ::std::dec << gba->ReadMem16(0x04000006)
         << "<br>";
      ss << "<b>DISPCNT:</b> 0x" << ::std::hex << ::std::setw(4)
         << gba->ReadMem16(0x04000000);
    } else if (currentEmulator == EmulatorType::Switch) {
      ss << switchEmulator->GetDebugInfo();
    }

    devPanelLabel->setText(QString::fromStdString(ss.str()));
  }

  // Scale and display
  // (Presentation moved above; keep this function responsible for producing
  // both emulator state updates and display output.)
}

} // namespace GUI
} // namespace AIO
