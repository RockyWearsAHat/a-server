#include "gui/MainWindow.h"

#include "emulator/switch/GpuCore.h"

#include "input/InputManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPixmap>
#include <QStringList>

#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace AIO {
namespace GUI {

static uint16_t ScriptKeyMaskFromName(const std::string& name) {
    // GBA KEYINPUT bits (0 = pressed): 0:A 1:B 2:Select 3:Start 4:Right 5:Left 6:Up 7:Down 8:R 9:L
    static const std::unordered_map<std::string, uint16_t> k = {
        {"A", 1u << 0},
        {"B", 1u << 1},
        {"SELECT", 1u << 2},
        {"START", 1u << 3},
        {"RIGHT", 1u << 4},
        {"LEFT", 1u << 5},
        {"UP", 1u << 6},
        {"DOWN", 1u << 7},
        {"R", 1u << 8},
        {"L", 1u << 9},
    };
    auto it = k.find(name);
    return (it == k.end()) ? 0 : it->second;
}

static const char* ScriptNameFromMask(uint16_t mask) {
    switch (mask) {
        case (1u << 0): return "A";
        case (1u << 1): return "B";
        case (1u << 2): return "SELECT";
        case (1u << 3): return "START";
        case (1u << 4): return "RIGHT";
        case (1u << 5): return "LEFT";
        case (1u << 6): return "UP";
        case (1u << 7): return "DOWN";
        case (1u << 8): return "R";
        case (1u << 9): return "L";
        default: return "?";
    }
}

static std::optional<std::vector<MainWindow::ScriptEvent>> LoadInputScriptMs(const QString& path) {
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
        if (hash != std::string::npos) line = line.substr(0, hash);

        std::istringstream iss(line);
        double ms = 0.0;
        std::string key;
        std::string action;
        if (!(iss >> ms >> key >> action)) {
            continue;
        }

        for (char& c : key) c = (char)std::toupper((unsigned char)c);
        for (char& c : action) c = (char)std::toupper((unsigned char)c);

        const uint16_t mask = ScriptKeyMaskFromName(key);
        if (mask == 0) {
            std::cout << "[SCRIPT] unknown key '" << key << "' at line " << lineNo << std::endl;
            continue;
        }

        const bool down = (action == "DOWN" || action == "PRESS" || action == "PRESSED");
        const bool up = (action == "UP" || action == "RELEASE" || action == "RELEASED");
        if (!down && !up) {
            std::cout << "[SCRIPT] unknown action '" << action << "' at line " << lineNo << std::endl;
            continue;
        }

        events.push_back(MainWindow::ScriptEvent{(int64_t)ms, mask, down});
    }

    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        if (a.ms != b.ms) return a.ms < b.ms;
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

void MainWindow::AddBreakpoint(uint32_t addr) {
    gba.AddBreakpoint(addr);
}

QString MainWindow::formatInputState(uint16_t state) {
    // GBA KEYINPUT: 0 = pressed, 1 = released
    // Bits: 0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L
    QStringList pressed;
    if (!(state & 0x001)) pressed << "A";
    if (!(state & 0x002)) pressed << "B";
    if (!(state & 0x004)) pressed << "SEL";
    if (!(state & 0x008)) pressed << "START";
    if (!(state & 0x010)) pressed << "→";
    if (!(state & 0x020)) pressed << "←";
    if (!(state & 0x040)) pressed << "↑";
    if (!(state & 0x080)) pressed << "↓";
    if (!(state & 0x100)) pressed << "R";
    if (!(state & 0x200)) pressed << "L";

    if (pressed.isEmpty()) {
        return "None";
    }
    return pressed.join(" + ");
}

void MainWindow::LoadROM(const std::string& path) {
    bool success = false;

    if (currentEmulator == EmulatorType::GBA) {
        success = gba.LoadROM(path);
        if (success) {
            // GBA Resolution
            displayImage = QImage(240, 160, QImage::Format_ARGB32);
            displayLabel->setFixedSize(240 * 2, 160 * 2);
        }
    } else if (currentEmulator == EmulatorType::Switch) {
        success = switchEmulator.LoadROM(path);
        if (success) {
            // Switch Resolution (720p)
            // Scale down for display if needed, or show smaller window
            displayImage = QImage(1280, 720, QImage::Format_ARGB32);
            displayLabel->setFixedSize(1280 / 2, 720 / 2); // 640x360
        }
    }

    if (success) {
        statusLabel->setText("ROM Loaded: " + QString::fromStdString(path));

        // Optional scripted input playback (debugging aid).
        inputScript_.clear();
        nextScriptEvent_ = 0;
        scriptKeyState_ = 0x03FF;
        scriptEnabled_ = false;

        if (!inputScriptPath_.isEmpty()) {
            auto loaded = LoadInputScriptMs(inputScriptPath_);
            if (loaded) {
                inputScript_ = std::move(*loaded);
                scriptEnabled_ = !inputScript_.empty();
                scriptTimer_.restart();
                std::cout << "[SCRIPT] loaded " << inputScript_.size() << " events from "
                          << inputScriptPath_.toStdString() << std::endl;
            } else {
                std::cout << "[SCRIPT] failed to open script: " << inputScriptPath_.toStdString() << std::endl;
            }
        }

        // Start emulator thread and display update timer
        StartEmulatorThread();
        displayTimer->start(16);  // ~60 Hz display updates

        // Switch to emulator view
        stackedWidget->setCurrentWidget(emulatorPage);

        // Ensure keyboard focus for input
        setFocus();
        activateWindow();
    } else {
        statusLabel->setText("Failed to load ROM");
    }
}

void MainWindow::SetInputScriptPath(const std::string& path) {
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
    emulatorThread = std::thread(&MainWindow::EmulatorThreadMain, this);
}

void MainWindow::StopEmulatorThread() {
    emulatorRunning = false;
    if (emulatorThread.joinable()) {
        emulatorThread.join();
    }
}

void MainWindow::EmulatorThreadMain() {
    // Emulator loop runs on background thread
    // Executes CPU cycles independent of Qt event processing
    auto frameStartTime = std::chrono::high_resolution_clock::now();
    static constexpr int kFrameTimeMs = 16;   // ~60 FPS target
    static constexpr int kTargetCyclesPerFrame = 280000; // ~16.7ms @ 16.78 MHz

    while (emulatorRunning) {
        if (emulatorPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        frameStartTime = std::chrono::high_resolution_clock::now();

        if (currentEmulator == EmulatorType::GBA) {
            int totalCycles = 0;

            while (totalCycles < kTargetCyclesPerFrame && emulatorRunning) {
                totalCycles += gba.Step();
            }

            // Periodically flush save
            saveFlushCounter++;
            if (saveFlushCounter >= SAVE_FLUSH_INTERVAL) {
                saveFlushCounter = 0;
                gba.GetMemory().FlushSave();
            }
        } else if (currentEmulator == EmulatorType::Switch) {
            switchEmulator.RunFrame();
        }

        // Sync to ~60 FPS: sleep for remaining frame time
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - frameStartTime
        ).count();
        int sleepMs = kFrameTimeMs - (int)elapsed;
        if (sleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

void MainWindow::UpdateDisplay() {
    // UI timer callback: update display from emulator state
    // Runs on main Qt thread at 60 Hz

    // Update Input
    // Polling is owned by the navigation timer; UI/emulator consume the latest snapshot.
    auto& input = AIO::Input::InputManager::instance();

    QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
    const bool inEmu = (current == emulatorPage) && emulatorRunning;
    input.setActiveContext(inEmu ? AIO::Input::InputContext::Emulator : AIO::Input::InputContext::UI);

    auto snapshot = input.snapshot();
    if (snapshot.logical == 0xFFFFFFFFu && snapshot.keyinput == 0x03FF && snapshot.system == 0) {
        snapshot = input.updateSnapshot();
    }

    uint16_t inputState = snapshot.keyinput;

    // Route input based on the active UI page.
    // current already computed above.

    // Two-layer input model:
    // - Our Application (menus/booter): driven by navTimer + NavigationController/UIActionMapper.
    // - Sub-applications (emulator runtime, streaming/web apps): may handle keys directly.
    // Important: do NOT drive menu navigation here as well, or we'll double-dispatch actions.
    const bool isSubAppPage = (current == emulatorPage) || (current == streamingHubPage) ||
                              (current == streamingWebPage) || (current == youTubeBrowsePage) ||
                              (current == youTubePlayerPage);

    const bool inStreamingUi = (current == streamingHubPage) || (current == streamingWebPage) ||
                               (current == youTubeBrowsePage) || (current == youTubePlayerPage);

    // Sub-app layer: synthesize basic keys for pages that rely on keyPressEvent.
    // Note: emulator runtime itself is fed via gba.UpdateInput below.
    if (isSubAppPage && current != emulatorPage) {
        // Fallback: synthesize key presses so existing keyPressEvent handlers work.
        // Includes repeat for held directions to make controller navigation consistent.
        QWidget* target = QApplication::focusWidget();
        if (!target) target = current ? current : this;
        if (target && target->focusProxy()) target = target->focusProxy();

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
        if (!uiRepeatTimer.isValid()) uiRepeatTimer.start();
        const qint64 nowMs = uiRepeatTimer.elapsed();

        constexpr qint64 INITIAL_DELAY_MS = 220;
        constexpr qint64 REPEAT_MS = 70;

        auto handleRepeatLogical = [&](AIO::Input::LogicalButton logical, int qtKey, RepeatState& st, uint32_t& lastLogical) {
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
        handleRepeatLogical(AIO::Input::LogicalButton::Left, Qt::Key_Left, repLeft, lastLogicalUi);
        handleRepeatLogical(AIO::Input::LogicalButton::Right, Qt::Key_Right, repRight, lastLogicalUi);
        handleRepeatLogical(AIO::Input::LogicalButton::Up, Qt::Key_Up, repUp, lastLogicalUi);
        handleRepeatLogical(AIO::Input::LogicalButton::Down, Qt::Key_Down, repDown, lastLogicalUi);

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
        if (inEmu && scriptEnabled_ && scriptTimer_.isValid()) {
            int64_t nowMs = 0;
            const QString timebase = qEnvironmentVariable("AIO_INPUT_SCRIPT_TIMEBASE").trimmed().toUpper();
            if (timebase == "EMU") {
                constexpr uint64_t CYCLES_PER_SECOND = 16780000ULL;
                nowMs = (int64_t)((gba.GetTotalCycles() * 1000ULL) / CYCLES_PER_SECOND);
            } else {
                nowMs = (int64_t)scriptTimer_.elapsed();
            }
            while (nextScriptEvent_ < inputScript_.size() && inputScript_[nextScriptEvent_].ms <= nowMs) {
                const auto& ev = inputScript_[nextScriptEvent_];
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
                std::cout << "[SCRIPT] t_ms=" << nowMs
                          << " event_ms=" << ev.ms
                          << " key=" << ScriptNameFromMask(ev.mask)
                          << " action=" << (ev.down ? "DOWN" : "UP")
                          << " keyState=0x" << std::hex << scriptKeyState_ << std::dec
                          << " pc=0x" << std::hex << gba.GetPC() << std::dec
                          << " DISPCNT=0x" << std::hex << dispcnt
                          << " WININ=0x" << winin
                          << " WINOUT=0x" << winout
                          << " WIN0H=0x" << win0h
                          << " WIN0V=0x" << win0v
                          << " BLDCNT=0x" << bldcnt
                          << " BLDALPHA=0x" << bldalpha
                          << std::dec
                          << std::endl;
                nextScriptEvent_++;
            }
            inputState = scriptKeyState_;
        }

        if (!inStreamingUi) {
            gba.UpdateInput(inputState);
        } else {
            // Release all keys when in streaming UI (KEYINPUT is active-low).
            gba.UpdateInput(0x03FF);
        }

        // Copy framebuffer to display image
        const auto& buffer = gba.GetPPU().GetFramebuffer();
        if ((int)buffer.size() >= 240 * 160) {
            for (int y = 0; y < 160; ++y) {
                const uint32_t* src = &buffer[y * 240];
                uchar* dst = displayImage.scanLine(y);
                memcpy(dst, src, 240 * sizeof(uint32_t));
            }
        }
    } else if (currentEmulator == EmulatorType::Switch) {
        auto* gpu = switchEmulator.GetGPU();
        if (gpu) {
            const auto& buffer = gpu->GetFramebuffer();
            if (buffer.size() >= 1280 * 720) {
                memcpy(displayImage.bits(), buffer.data(), buffer.size() * sizeof(uint32_t));
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
        ss << "<b>FPS:</b> " << ::std::fixed << ::std::setprecision(1) << currentFPS << "<br>";

        if (currentEmulator == EmulatorType::GBA) {
            uint16_t gameKeyInput = gba.ReadMem16(0x04000130);
            ss << "<b>PC:</b> 0x" << ::std::hex << ::std::setfill('0') << ::std::setw(8) << gba.GetPC() << "<br>";
            ss << "<b>Input:</b> " << formatInputState(inputState).toStdString() << "<br>";
            ss << "<b>KEYINPUT:</b> 0x" << ::std::hex << ::std::setw(4) << gameKeyInput << "<br>";
            ss << "<b>VCount:</b> " << ::std::dec << gba.ReadMem16(0x04000006) << "<br>";
            ss << "<b>DISPCNT:</b> 0x" << ::std::hex << ::std::setw(4) << gba.ReadMem16(0x04000000);
        } else if (currentEmulator == EmulatorType::Switch) {
            ss << switchEmulator.GetDebugInfo();
        }

        devPanelLabel->setText(QString::fromStdString(ss.str()));
    }

    // Scale and display
    displayLabel->setPixmap(QPixmap::fromImage(displayImage).scaled(displayLabel->size(), Qt::KeepAspectRatio));
}

} // namespace GUI
} // namespace AIO
