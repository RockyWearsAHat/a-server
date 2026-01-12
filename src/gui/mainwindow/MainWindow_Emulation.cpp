#include "gui/MainWindow.h"

#include "emulator/switch/GpuCore.h"

#include "emulator/common/Logger.h"

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
      "DumpCurrentFramePPM: wrote %dx%d PPM to '%s' (nonBlackRatio=%.6f)", w,
      h, path.c_str(), ratio);
  return true;
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
    gba.SetSingleStep(true);
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
    gba.SetSingleStep(false);
    if (stdinRawEnabled) {
      tcsetattr(STDIN_FILENO, TCSANOW, &rawTermios);
      stdinRawEnabled = false;
    }
  }
}

void MainWindow::AddBreakpoint(uint32_t addr) { gba.AddBreakpoint(addr); }

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
    success = gba.LoadROM(path);
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
  } else if (currentEmulator == EmulatorType::Switch) {
    success = switchEmulator.LoadROM(path);
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
  }
}

void MainWindow::StartEmulatorThread() {
  if (emulatorRunning.exchange(true)) {
    return; // Already running
  }

  // Lazily initialize audio on first emulator run to avoid blocking app
  // startup.
  if (audioDevice == 0 && currentEmulator == EmulatorType::GBA) {
    initAudio();
  }

  emulatorThread = std::thread(&MainWindow::EmulatorThreadMain, this);

  // Start audio immediately when emulation begins.
  // The APU ring buffer already returns silence on underrun, so delaying audio
  // start just creates an artificial "startup silence".
  if (audioDevice != 0 && currentEmulator == EmulatorType::GBA) {
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
    gba.GetMemory().FlushSave();
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
      gba.UpdateInput(desired);
      lastAppliedKeyinput = desired;
    }
  };

  while (emulatorRunning) {
    if (emulatorPaused) {
      nextFrame = Clock::now();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    if (currentEmulator == EmulatorType::GBA) {
      int totalCycles = 0;

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
          const int stepCycles = gba.Step();
          chunkCycles += stepCycles;
          totalCycles += stepCycles;
        }
      }

      // Catch any remainder cycles due to integer division.
      while (totalCycles < kGbaCyclesPerFrame && emulatorRunning) {
        totalCycles += gba.Step();
      }

      applyPendingKeyinput();

      // Periodically flush save
      saveFlushCounter++;
      if (saveFlushCounter >= SAVE_FLUSH_INTERVAL) {
        saveFlushCounter = 0;
        gba.GetMemory().FlushSave();
      }
    } else if (currentEmulator == EmulatorType::Switch) {
      switchEmulator.RunFrame();
    }

    // Advance deadline (pick duration based on active emulator).
    const auto frameDur =
        (currentEmulator == EmulatorType::GBA)
            ? std::chrono::duration_cast<Clock::duration>(gbaFrameDuration)
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
      (current == emulatorPage) || (current == streamingHubPage) ||
      (current == streamingWebPage) || (current == youTubeBrowsePage) ||
      (current == youTubePlayerPage);

  const bool inStreamingUi =
      (current == streamingHubPage) || (current == streamingWebPage) ||
      (current == youTubeBrowsePage) || (current == youTubePlayerPage);

  // Sub-app layer: synthesize basic keys for pages that rely on keyPressEvent.
  // Note: emulator runtime itself is fed via gba.UpdateInput below.
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
        nowMs = (int64_t)((gba.GetTotalCycles() * 1000ULL) / CYCLES_PER_SECOND);
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

        const uint16_t dispcnt = gba.ReadMem16(0x04000000);
        const uint16_t winin = gba.ReadMem16(0x04000048);
        const uint16_t winout = gba.ReadMem16(0x0400004A);
        const uint16_t bldcnt = gba.ReadMem16(0x04000050);
        const uint16_t bldalpha = gba.ReadMem16(0x04000052);
        const uint16_t win0h = gba.ReadMem16(0x04000040);
        const uint16_t win0v = gba.ReadMem16(0x04000044);
        std::cout << "[SCRIPT] t_ms=" << nowMs << " event_ms=" << ev.ms
                  << " key=" << ScriptNameFromMask(ev.mask)
                  << " action=" << (ev.down ? "DOWN" : "UP") << " keyState=0x"
                  << std::hex << scriptKeyState_ << std::dec << " pc=0x"
                  << std::hex << gba.GetPC() << std::dec << " DISPCNT=0x"
                  << std::hex << dispcnt << " WININ=0x" << winin << " WINOUT=0x"
                  << winout << " WIN0H=0x" << win0h << " WIN0V=0x" << win0v
                  << " BLDCNT=0x" << bldcnt << " BLDALPHA=0x" << bldalpha
                  << std::dec << std::endl;
        nextScriptEvent_++;
      }
      inputState = scriptKeyState_;
    }

    // Copy framebuffer to display image
    const auto &buffer = gba.GetPPU().GetFramebuffer();
    if ((int)buffer.size() >= 240 * 160) {
      for (int y = 0; y < 160; ++y) {
        const uint32_t *src = &buffer[y * 240];
        uchar *dst = displayImage.scanLine(y);
        memcpy(dst, src, 240 * sizeof(uint32_t));
      }
    }
  } else if (currentEmulator == EmulatorType::Switch) {
    auto *gpu = switchEmulator.GetGPU();
    if (gpu) {
      const auto &buffer = gpu->GetFramebuffer();
      if (buffer.size() >= 1280 * 720) {
        memcpy(displayImage.bits(), buffer.data(),
               buffer.size() * sizeof(uint32_t));
      }
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

      const auto mode = (videoScaleMode_ == VideoScaleMode::FitNearest)
                            ? AIO::Common::ScaleMode::FitNearest
                            : AIO::Common::ScaleMode::IntegerNearest;

      const auto scaled = AIO::Common::ComputeScaledSize(
          srcW, srcH, targetW, targetH, mode, videoIntegerScale_);

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
    ss << "<b>FPS:</b> " << ::std::fixed << ::std::setprecision(1) << currentFPS
       << "<br>";

    if (currentEmulator == EmulatorType::GBA) {
      uint16_t gameKeyInput = gba.ReadMem16(0x04000130);
      ss << "<b>PC:</b> 0x" << ::std::hex << ::std::setfill('0')
         << ::std::setw(8) << gba.GetPC() << "<br>";
      ss << "<b>Input:</b> " << formatInputState(inputState).toStdString()
         << "<br>";
      ss << "<b>KEYINPUT:</b> 0x" << ::std::hex << ::std::setw(4)
         << gameKeyInput << "<br>";
      ss << "<b>VCount:</b> " << ::std::dec << gba.ReadMem16(0x04000006)
         << "<br>";
      ss << "<b>DISPCNT:</b> 0x" << ::std::hex << ::std::setw(4)
         << gba.ReadMem16(0x04000000);
    } else if (currentEmulator == EmulatorType::Switch) {
      ss << switchEmulator.GetDebugInfo();
    }

    devPanelLabel->setText(QString::fromStdString(ss.str()));
  }

  // Scale and display
  // (Presentation moved above; keep this function responsible for producing
  // both emulator state updates and display output.)
}

} // namespace GUI
} // namespace AIO
