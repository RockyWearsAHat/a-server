#include <QApplication>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QPushButton>
#include <QListWidget>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QCoreApplication>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <QDirIterator>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <SDL2/SDL.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

// Include all project headers BEFORE opening namespace
#include "gui/MainWindow.h"
#include "gui/LogViewerDialog.h"
#include "gui/InputConfigDialog.h"
#include "gui/StreamingHubWidget.h"
#include "gui/StreamingWebViewPage.h"
#include "gui/YouTubeBrowsePage.h"
#include "gui/YouTubePlayerPage.h"
#include "input/InputManager.h"
#include "emulator/common/Logger.h"
#include "emulator/gba/ARM7TDMI.h"
#include "emulator/switch/GpuCore.h"

// Helper function at global scope to avoid Qt template instantiation issues
static bool crashPopupShown = false;

static void ShowCrashPopup(const char* logPath) {
    if (crashPopupShown) return;
    crashPopupShown = true;
    QMessageBox msg;
    msg.setWindowTitle("Emulator Crash Detected");
    msg.setText("The emulator has crashed. A detailed log has been saved.\nWould you like to view the log?");
    msg.setIcon(QMessageBox::Critical);
    msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    int ret = msg.exec();
    if (ret == QMessageBox::Yes) {
        AIO::GUI::LogViewerDialog* dlg = new AIO::GUI::LogViewerDialog(nullptr);
        dlg->loadLogFile(QString::fromUtf8(logPath));
        dlg->exec();
        delete dlg;
    }
    QApplication::quit();
}

namespace AIO {
namespace GUI {

static MainWindow* audioInstance = nullptr;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), settings("AIOServer", "GBAEmulator")
{
    // Load unified 10-foot UI theme.
    QString styleSheet;
    QFile f(QCoreApplication::applicationDirPath() + "/../assets/qss/tv.qss");
    if (!f.exists()) {
        // Fallback for running from build tree.
        f.setFileName(QCoreApplication::applicationDirPath() + "/../../assets/qss/tv.qss");
    }
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        styleSheet = ts.readAll();
        f.close();
    }

    if (!styleSheet.isEmpty()) {
        setStyleSheet(styleSheet);
    }
    loadSettings();
    stackedWidget = new QStackedWidget(this);
    setCentralWidget(stackedWidget);
    setupMainMenu();
    setupEmulatorSelect();
    setupGameSelect();
    setupEmulatorView();
    setupSettingsPage();
    setupStreamingPages();
    stackedWidget->addWidget(mainMenuPage);
    stackedWidget->addWidget(emulatorSelectPage);
    stackedWidget->addWidget(gameSelectPage);
    stackedWidget->addWidget(emulatorPage);
    stackedWidget->addWidget(settingsPage);
    stackedWidget->addWidget(streamingHubPage);
    stackedWidget->addWidget(youTubeBrowsePage);
    stackedWidget->addWidget(youTubePlayerPage);
    stackedWidget->addWidget(streamingWebPage);
    stackedWidget->setCurrentWidget(mainMenuPage);
    
    // Display update timer: UI refresh at 60 Hz (not CPU stepping)
    displayTimer = new QTimer(this);
    connect(displayTimer, &QTimer::timeout, this, &MainWindow::UpdateDisplay);
    
    displayImage = QImage(240, 160, QImage::Format_ARGB32);
    displayImage.fill(Qt::black);
    setFocusPolicy(Qt::StrongFocus);
    setFocus();
    fpsTimer.start();
    initAudio();
}

MainWindow::~MainWindow() {
    StopEmulatorThread();
    closeAudio();
}

void MainWindow::initAudio() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        ::std::cerr << "SDL audio init failed: " << SDL_GetError() << ::std::endl;
        return;
    }
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = AUDIO_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;  // Signed 16-bit, system byte order
        want.channels = 2;  // Stereo
        want.samples = AUDIO_BUFFER_SIZE;
        want.callback = audioCallback;
        want.userdata = this;

        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDevice == 0) {
            ::std::cerr << "SDL audio device open failed: " << SDL_GetError() << ::std::endl;
            return;
        }

        ::std::cout << "Audio initialized: " << have.freq << " Hz, " 
                  << (int)have.channels << " channels, buffer " << have.samples << ::std::endl;

        // Start audio playback
        SDL_PauseAudioDevice(audioDevice, 0);
        audioInstance = this;
    }

    void MainWindow::closeAudio() {
        if (audioDevice != 0) {
            SDL_CloseAudioDevice(audioDevice);
            audioDevice = 0;
        }
        audioInstance = nullptr;
    }

    void MainWindow::audioCallback(void* userdata, Uint8* stream, int len) {
        MainWindow* self = static_cast<MainWindow*>(userdata);
        int16_t* buffer = reinterpret_cast<int16_t*>(stream);
        int numSamples = len / 4;  // stereo samples (2 channels * 2 bytes)
        
        if (self->currentEmulator == EmulatorType::GBA) {
            // Get samples from APU ring buffer
            self->gba.GetAPU().GetSamples(buffer, numSamples);
        } else {
            // Silence for now
            memset(stream, 0, len);
        }
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

    void MainWindow::SetEmulatorType(int type) {
        if (type == 0) {
            currentEmulator = EmulatorType::GBA;
            std::cout << "[MainWindow] Set emulator type to GBA" << std::endl;
        } else if (type == 1) {
            currentEmulator = EmulatorType::Switch;
            std::cout << "[MainWindow] Set emulator type to Switch" << std::endl;
        }
    }

    void MainWindow::GameLoop() {
        // DEPRECATED: This function is now replaced by threading model
        // Kept for compatibility, but UpdateDisplay() handles UI now
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
        const int FRAME_TIME_MS = 16;  // ~60 FPS target
        
        while (emulatorRunning) {
            if (emulatorPaused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            frameStartTime = std::chrono::high_resolution_clock::now();
            
            if (currentEmulator == EmulatorType::GBA) {
                // Run one frame's worth of cycles (280k @ 16.78 MHz = ~16.7ms)
                int totalCycles = 0;
                const int TARGET_CYCLES = 280000;
                
                while (totalCycles < TARGET_CYCLES && emulatorRunning) {
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
            int sleepMs = FRAME_TIME_MS - (int)elapsed;
            if (sleepMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }
    }

    void MainWindow::UpdateDisplay() {
        // UI timer callback: update display from emulator state
        // Runs on main Qt thread at 60 Hz
        
        // Update Input
        uint16_t inputState = AIO::Input::InputManager::instance().update();

        // Route input based on the active UI page.
        // When the user is in streaming UI, do not feed the emulator.
        QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
        const bool inStreamingUi = (current == streamingHubPage) || (current == streamingWebPage) ||
                                  (current == youTubeBrowsePage) || (current == youTubePlayerPage);

        // Feed controller navigation into UI pages.
        // Pages that implement explicit controller handlers:
        if (current == youTubeBrowsePage) {
            if (auto* yt = qobject_cast<YouTubeBrowsePage*>(youTubeBrowsePage)) yt->onControllerInput(inputState);
        } else if (current == youTubePlayerPage) {
            if (auto* p = qobject_cast<YouTubePlayerPage*>(youTubePlayerPage)) p->onControllerInput(inputState);
        } else {
            // Fallback: synthesize key presses so existing keyPressEvent handlers work.
            // Includes repeat for held directions to make controller navigation consistent.
            QWidget* target = QApplication::focusWidget();
            if (!target) target = current ? current : this;
            if (target && target->focusProxy()) target = target->focusProxy();

            auto sendKey = [&](int qtKey) {
                QKeyEvent ev(QEvent::KeyPress, qtKey, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &ev);
            };

            auto pressed = [&](int bit) { return (inputState & (1u << bit)) == 0; };

            // Persistent UI controller state for repeat handling.
            struct RepeatState {
                bool down = false;
                qint64 nextMs = 0;
            };
            static uint16_t lastUiState = 0x03FF;
            static RepeatState repLeft, repRight, repUp, repDown;
            static QElapsedTimer uiRepeatTimer;
            if (!uiRepeatTimer.isValid()) uiRepeatTimer.start();
            const qint64 nowMs = uiRepeatTimer.elapsed();

            constexpr qint64 INITIAL_DELAY_MS = 220;
            constexpr qint64 REPEAT_MS = 70;

            auto handleRepeat = [&](int bit, int qtKey, RepeatState& st) {
                const bool isDown = pressed(bit);
                const bool wasDown = (lastUiState & (1u << bit)) == 0;

                if (isDown && !wasDown) {
                    // Initial press
                    st.down = true;
                    st.nextMs = nowMs + INITIAL_DELAY_MS;
                    sendKey(qtKey);
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
            };

            handleRepeat(AIO::Input::Button_Left, Qt::Key_Left, repLeft);
            handleRepeat(AIO::Input::Button_Right, Qt::Key_Right, repRight);
            handleRepeat(AIO::Input::Button_Up, Qt::Key_Up, repUp);
            handleRepeat(AIO::Input::Button_Down, Qt::Key_Down, repDown);

            // Buttons (edge-triggered)
            auto edge = [&](int bit) {
                const bool isDown = pressed(bit);
                const bool wasDown = ((lastUiState & (1u << bit)) == 0);
                return isDown && !wasDown;
            };

            if (edge(AIO::Input::Button_A)) sendKey(Qt::Key_Return);
            if (edge(AIO::Input::Button_B)) sendKey(Qt::Key_Escape);

            lastUiState = inputState;
        }
        
        if (currentEmulator == EmulatorType::GBA) {
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

    void MainWindow::keyPressEvent(QKeyEvent *event) {
        if (Input::InputManager::instance().processKeyEvent(event)) {
            // Input handled
        } else {
            // Debugger controls via GUI
            if (debuggerEnabled) {
                if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                    gba.SetSingleStep(true);
                    gba.Step();
                } else if (event->key() == Qt::Key_Up) {
                    gba.StepBack();
                } else if (event->key() == Qt::Key_C) {
                    debuggerContinue = true;
                }
            }
            QMainWindow::keyPressEvent(event);
        }
    }

    void MainWindow::keyReleaseEvent(QKeyEvent *event) {
        if (Input::InputManager::instance().processKeyEvent(event)) {
            // Input handled
        } else {
            QMainWindow::keyReleaseEvent(event);
        }
    }

    void MainWindow::setupMainMenu() {
        mainMenuPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(mainMenuPage);
        layout->setContentsMargins(50, 50, 50, 50);
        layout->setSpacing(20);
        
        QLabel *title = new QLabel("AIO ENTERTAINMENT SYSTEM", mainMenuPage);
        title->setAlignment(Qt::AlignCenter);
        title->setProperty("role", "title");
        layout->addWidget(title);

        QPushButton *emuBtn = new QPushButton("EMULATORS", mainMenuPage);
        emuBtn->setCursor(Qt::PointingHandCursor);
        emuBtn->setFocusPolicy(Qt::StrongFocus);
        connect(emuBtn, &QPushButton::clicked, this, &MainWindow::goToEmulatorSelect);
        layout->addWidget(emuBtn);

        QPushButton *streamBtn = new QPushButton("STREAMING", mainMenuPage);
        streamBtn->setCursor(Qt::PointingHandCursor);
        streamBtn->setFocusPolicy(Qt::StrongFocus);
        connect(streamBtn, &QPushButton::clicked, this, &MainWindow::openStreaming);
        layout->addWidget(streamBtn);

        QPushButton *settingsBtn = new QPushButton("SETTINGS", mainMenuPage);
        settingsBtn->setCursor(Qt::PointingHandCursor);
        settingsBtn->setFocusPolicy(Qt::StrongFocus);
        connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::goToSettings);
        layout->addWidget(settingsBtn);

        layout->addStretch();
        
        QLabel *footer = new QLabel("v1.0.0 | System Ready", mainMenuPage);
        footer->setAlignment(Qt::AlignCenter);
        footer->setProperty("role", "subtitle");
        layout->addWidget(footer);
    }

    void MainWindow::openStreaming() {
        stackedWidget->setCurrentWidget(streamingHubPage);
        streamingHubPage->setFocus();
    }

    void MainWindow::setupStreamingPages() {
        auto* hub = new StreamingHubWidget(this);
        streamingHubPage = hub;

        auto* yt = new YouTubeBrowsePage(this);
        youTubeBrowsePage = yt;

        auto* ytPlayer = new YouTubePlayerPage(this);
        youTubePlayerPage = ytPlayer;

        auto* web = new StreamingWebViewPage(this);
        streamingWebPage = web;

        connect(hub, &StreamingHubWidget::launchRequested, this, [this](AIO::GUI::StreamingApp app) {
            launchStreamingApp(static_cast<int>(app));
        });
        connect(web, &StreamingWebViewPage::homeRequested, this, [this]() {
            stackedWidget->setCurrentWidget(streamingHubPage);
            streamingHubPage->setFocus();
        });

        connect(yt, &YouTubeBrowsePage::homeRequested, this, [this]() {
            stackedWidget->setCurrentWidget(streamingHubPage);
            streamingHubPage->setFocus();
        });

        connect(yt, &YouTubeBrowsePage::videoRequested, this, [this](const QString& url) {
            auto* player = qobject_cast<YouTubePlayerPage*>(youTubePlayerPage);
            if (!player) return;
            stackedWidget->setCurrentWidget(youTubePlayerPage);
            player->playVideoUrl(url);
            youTubePlayerPage->setFocus();
        });

        connect(ytPlayer, &YouTubePlayerPage::homeRequested, this, [this]() {
            stackedWidget->setCurrentWidget(streamingHubPage);
            streamingHubPage->setFocus();
        });
        connect(ytPlayer, &YouTubePlayerPage::backRequested, this, [this]() {
            stackedWidget->setCurrentWidget(youTubeBrowsePage);
            youTubeBrowsePage->setFocus();
        });
    }

    void MainWindow::launchStreamingApp(int app) {
        const auto selectedApp = static_cast<AIO::GUI::StreamingApp>(app);

        // YouTube uses the Data API + native Qt UI (no WebEngine).
        if (selectedApp == AIO::GUI::StreamingApp::YouTube) {
            if (!youTubeBrowsePage) return;
            stackedWidget->setCurrentWidget(youTubeBrowsePage);
            youTubeBrowsePage->setFocus();
            return;
        }

        auto* web = qobject_cast<StreamingWebViewPage*>(streamingWebPage);
        if (!web) return;
        stackedWidget->setCurrentWidget(streamingWebPage);
        web->openApp(selectedApp);
        streamingWebPage->setFocus();
    }

    void MainWindow::setupSettingsPage() {
        settingsPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(settingsPage);
        layout->setContentsMargins(50, 50, 50, 50);

        QLabel *title = new QLabel("SYSTEM SETTINGS", settingsPage);
        title->setAlignment(Qt::AlignCenter);
        title->setProperty("role", "title");
        layout->addWidget(title);

        // ROM Directory Setting
        QGroupBox *romGroup = new QGroupBox("ROM Library Path", settingsPage);
        QVBoxLayout *romLayout = new QVBoxLayout(romGroup);
        
        romPathLabel = new QLabel(romDirectory, romGroup);
        romPathLabel->setWordWrap(true);
        romPathLabel->setObjectName("aioPathLabel");
        romLayout->addWidget(romPathLabel);

        QPushButton *browseBtn = new QPushButton("BROWSE FOLDER...", romGroup);
        browseBtn->setCursor(Qt::PointingHandCursor);
        browseBtn->setFocusPolicy(Qt::StrongFocus);
        connect(browseBtn, &QPushButton::clicked, this, &MainWindow::selectRomDirectory);
        romLayout->addWidget(browseBtn);

        layout->addWidget(romGroup);

        layout->addStretch();

        QPushButton *backBtn = new QPushButton("BACK TO MENU", settingsPage);
        backBtn->setCursor(Qt::PointingHandCursor);
        backBtn->setFocusPolicy(Qt::StrongFocus);
        backBtn->setProperty("variant", "secondary");
        connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);
        layout->addWidget(backBtn);
    }

    void MainWindow::setupEmulatorSelect() {
        emulatorSelectPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(emulatorSelectPage);
        layout->setContentsMargins(50, 50, 50, 50);
        layout->setSpacing(20);

        QLabel *title = new QLabel("SELECT SYSTEM", emulatorSelectPage);
        title->setAlignment(Qt::AlignCenter);
        title->setProperty("role", "title");
        layout->addWidget(title);

        QPushButton *gbaBtn = new QPushButton("GAME BOY ADVANCE", emulatorSelectPage);
        gbaBtn->setCursor(Qt::PointingHandCursor);
        gbaBtn->setFocusPolicy(Qt::StrongFocus);
        gbaBtn->setStyleSheet("text-align: left; padding-left: 24px; border-left: 6px solid #8b5cf6;"); 
        connect(gbaBtn, &QPushButton::clicked, this, [this]() {
            currentEmulator = EmulatorType::GBA;
            goToGameSelect();
        });
        layout->addWidget(gbaBtn);

        QPushButton *switchBtn = new QPushButton("NINTENDO SWITCH", emulatorSelectPage);
        switchBtn->setCursor(Qt::PointingHandCursor);
        switchBtn->setFocusPolicy(Qt::StrongFocus);
        switchBtn->setStyleSheet("text-align: left; padding-left: 24px; border-left: 6px solid #ff4d4d;");
        connect(switchBtn, &QPushButton::clicked, this, [this]() {
            currentEmulator = EmulatorType::Switch;
            goToGameSelect();
        });
        layout->addWidget(switchBtn);

        layout->addStretch();

        QPushButton *backBtn = new QPushButton("BACK", emulatorSelectPage);
        backBtn->setCursor(Qt::PointingHandCursor);
        backBtn->setFocusPolicy(Qt::StrongFocus);
        backBtn->setProperty("variant", "secondary");
        connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);
        layout->addWidget(backBtn);
    }

    void MainWindow::refreshGameList() {
        gameListWidget->clear();
        
        QDir dir(romDirectory);
        if (!dir.exists()) {
            gameListWidget->addItem("Error: Invalid ROM Directory");
            return;
        }

        QStringList filters;
        if (currentEmulator == EmulatorType::GBA) {
            filters << "*.gba";
        } else if (currentEmulator == EmulatorType::Switch) {
            filters << "*.nso" << "*.nro" << "*.xci" << "*.nsp";
        }
        
        QDirIterator it(romDirectory, filters, QDir::Files, QDirIterator::Subdirectories);
        bool foundAny = false;
        
        while (it.hasNext()) {
            it.next();
            QFileInfo fileInfo = it.fileInfo();
            foundAny = true;

            QListWidgetItem *item = new QListWidgetItem();
            item->setText(fileInfo.completeBaseName());
            item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
            
            // Generate Placeholder Icon
            QPixmap pixmap(180, 120);
            pixmap.fill(QColor("#444"));
            QPainter painter(&pixmap);
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setPixelSize(20);
            font.setBold(true);
            painter.setFont(font);
            
            QString sysName = (currentEmulator == EmulatorType::GBA) ? "GBA" : "NSW";
            painter.drawText(pixmap.rect().adjusted(0, -20, 0, 0), Qt::AlignCenter, sysName);
            
            font.setPixelSize(40);
            painter.setFont(font);
            QString initial = fileInfo.completeBaseName().left(1).toUpper();
            painter.drawText(pixmap.rect().adjusted(0, 20, 0, 0), Qt::AlignCenter, initial);
            
            painter.end();
            
            item->setIcon(QIcon(pixmap));
            gameListWidget->addItem(item);
        }

        if (!foundAny) {
            gameListWidget->addItem("No ROMs found in " + romDirectory);
        }
    }

    void MainWindow::startGame(const QString& path) {
        LoadROM(path.toStdString());
        stackedWidget->setCurrentWidget(emulatorPage);
        setFocus(); // Ensure window has focus for input
    }

    void MainWindow::stopGame() {
        StopEmulatorThread();
        displayTimer->stop();
        goToGameSelect();
    }

    void MainWindow::goToMainMenu() {
        stackedWidget->setCurrentWidget(mainMenuPage);
    }

    void MainWindow::goToSettings() {
        stackedWidget->setCurrentWidget(settingsPage);
    }

    void MainWindow::goToEmulatorSelect() {
        stackedWidget->setCurrentWidget(emulatorSelectPage);
    }

    void MainWindow::goToGameSelect() {
        refreshGameList();
        stackedWidget->setCurrentWidget(gameSelectPage);
    }

    void MainWindow::setupGameSelect() {
        gameSelectPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(gameSelectPage);
        layout->setContentsMargins(50, 50, 50, 50);

        QLabel *title = new QLabel("SELECT GAME", gameSelectPage);
        title->setAlignment(Qt::AlignCenter);
        title->setProperty("role", "title");
        layout->addWidget(title);

        gameListWidget = new QListWidget(gameSelectPage);
        gameListWidget->setFocusPolicy(Qt::StrongFocus);
        gameListWidget->setIconSize(QSize(180, 120));
        gameListWidget->setViewMode(QListWidget::IconMode);
        gameListWidget->setResizeMode(QListWidget::Adjust);
        gameListWidget->setSpacing(15);
        gameListWidget->setMovement(QListWidget::Static);
        
        connect(gameListWidget, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
            QString fullPath = item->data(Qt::UserRole).toString();
            startGame(fullPath);
        });

        layout->addWidget(gameListWidget);

        QPushButton *backBtn = new QPushButton("BACK", gameSelectPage);
        backBtn->setCursor(Qt::PointingHandCursor);
        backBtn->setFocusPolicy(Qt::StrongFocus);
        backBtn->setProperty("variant", "secondary");
        connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToEmulatorSelect);
        layout->addWidget(backBtn);
    }

    void MainWindow::setupEmulatorView() {
        emulatorPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(emulatorPage);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Top Bar (Menu)
        QWidget *topBar = new QWidget(emulatorPage);
        topBar->setObjectName("aioTopBar");
        topBar->setFixedHeight(40);
        QHBoxLayout *topLayout = new QHBoxLayout(topBar);
        topLayout->setContentsMargins(10, 0, 10, 0);

        QPushButton *stopBtn = new QPushButton("STOP", topBar);
        stopBtn->setFixedSize(80, 30);
        stopBtn->setProperty("variant", "secondary");
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stopGame);
        topLayout->addWidget(stopBtn);

        statusLabel = new QLabel("Ready", topBar);
        statusLabel->setProperty("role", "subtitle");
        topLayout->addWidget(statusLabel);
        
        topLayout->addStretch();

        QPushButton *devBtn = new QPushButton("DEV", topBar);
        devBtn->setCheckable(true);
        devBtn->setFixedSize(60, 30);
        devBtn->setProperty("variant", "secondary");
        connect(devBtn, &QPushButton::toggled, this, &MainWindow::toggleDevPanel);
        topLayout->addWidget(devBtn);

        layout->addWidget(topBar);

        // Game Area
        QWidget *gameArea = new QWidget(emulatorPage);
        QHBoxLayout *gameLayout = new QHBoxLayout(gameArea);
        gameLayout->setContentsMargins(0, 0, 0, 0);
        gameLayout->setSpacing(0);

        // Display
        displayLabel = new QLabel(gameArea);
        displayLabel->setAlignment(Qt::AlignCenter);
        displayLabel->setObjectName("aioDisplaySurface");
        displayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        gameLayout->addWidget(displayLabel);

        // Dev Panel (Overlay or Side)
        devPanelLabel = new QLabel(gameArea);
        devPanelLabel->setObjectName("aioDevPanel");
        devPanelLabel->setFixedWidth(250);
        devPanelLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        devPanelLabel->setVisible(false);
        gameLayout->addWidget(devPanelLabel);

        layout->addWidget(gameArea);
    }

    void MainWindow::loadSettings() {
        romDirectory = settings.value("romDirectory", QDir::homePath()).toString();
    }

    void MainWindow::selectRomDirectory() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select ROM Directory", romDirectory);
        if (!dir.isEmpty()) {
            romDirectory = dir;
            settings.setValue("romDirectory", romDirectory);
            romPathLabel->setText(romDirectory);
        }
    }

} // namespace GUI
} // namespace AIO
