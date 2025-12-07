#include "gui/MainWindow.h"
#include "input/InputManager.h"
#include "gui/InputConfigDialog.h"
#include "emulator/switch/GpuCore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <iostream>
#include <QKeyEvent>
#include <QApplication>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <QStackedWidget>
#include <QPushButton>
#include <QListWidget>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <QDirIterator>

namespace AIO::GUI {

    // Static pointer for audio callback (SDL uses C callback)
    static MainWindow* audioInstance = nullptr;

    MainWindow::MainWindow(QWidget *parent) 
        : QMainWindow(parent), settings("AIOServer", "GBAEmulator") 
    {
        setWindowTitle("AIO Server");
        resize(800, 600); 

        // --- UI THEME ---
        // Dark Theme for "Entertainment Center" look
        QString styleSheet = R"(
            QMainWindow {
                background-color: #121212;
                color: #ffffff;
            }
            QWidget {
                background-color: #121212;
                color: #ffffff;
                font-family: "Segoe UI", "Helvetica Neue", sans-serif;
                font-size: 16px;
            }
            QLabel {
                color: #e0e0e0;
            }
            QPushButton {
                background-color: #2d2d2d;
                color: #ffffff;
                border: 1px solid #3d3d3d;
                border-radius: 8px;
                padding: 15px;
                font-size: 18px;
                font-weight: bold;
                min-height: 50px;
            }
            QPushButton:hover {
                background-color: #3d3d3d;
                border: 1px solid #505050;
            }
            QPushButton:pressed {
                background-color: #505050;
            }
            QListWidget {
                background-color: #1e1e1e;
                border: 1px solid #333;
                border-radius: 8px;
                padding: 10px;
                font-size: 18px;
            }
            QListWidget::item {
                padding: 10px;
                border-bottom: 1px solid #2a2a2a;
            }
            QListWidget::item:selected {
                background-color: #3d3d3d;
                color: #ffffff;
            }
            QGroupBox {
                border: 1px solid #333;
                border-radius: 8px;
                margin-top: 20px;
                font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
        )";
        setStyleSheet(styleSheet);
        // ----------------

        loadSettings();

        stackedWidget = new QStackedWidget(this);
        setCentralWidget(stackedWidget);

        setupMainMenu();
        setupEmulatorSelect();
        setupGameSelect();
        setupEmulatorView();
        setupSettingsPage();

        stackedWidget->addWidget(mainMenuPage);
        stackedWidget->addWidget(emulatorSelectPage);
        stackedWidget->addWidget(gameSelectPage);
        stackedWidget->addWidget(emulatorPage);
        stackedWidget->addWidget(settingsPage);

        stackedWidget->setCurrentWidget(mainMenuPage);

        gameTimer = new QTimer(this);
        connect(gameTimer, &QTimer::timeout, this, &MainWindow::GameLoop);

        // Initialize display image
        displayImage = QImage(240, 160, QImage::Format_ARGB32);
        displayImage.fill(Qt::black);
        
        // Ensure main window can receive keyboard focus
        setFocusPolicy(Qt::StrongFocus);
        setFocus();
        
        // Initialize FPS timer
        fpsTimer.start();
        
        // Initialize SDL audio
        initAudio();
    }

    MainWindow::~MainWindow() {
        closeAudio();
    }

    void MainWindow::initAudio() {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            std::cerr << "SDL audio init failed: " << SDL_GetError() << std::endl;
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
            std::cerr << "SDL audio device open failed: " << SDL_GetError() << std::endl;
            return;
        }

        std::cout << "Audio initialized: " << have.freq << " Hz, " 
                  << (int)have.channels << " channels, buffer " << have.samples << std::endl;

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
            
            // 60 FPS roughly = 16ms
            gameTimer->start(16);
            
            // Ensure keyboard focus for input
            setFocus();
            activateWindow();
        } else {
            statusLabel->setText("Failed to load ROM");
        }
    }

    void MainWindow::GameLoop() {
        // Update Input
        uint16_t inputState = Input::InputManager::instance().update();
        
        if (currentEmulator == EmulatorType::GBA) {
            gba.UpdateInput(inputState);
        }
        // Switch input handling TODO

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
            std::stringstream ss;
            ss << "<b>FPS:</b> " << std::fixed << std::setprecision(1) << currentFPS << "<br>";
            
            if (currentEmulator == EmulatorType::GBA) {
                uint16_t gameKeyInput = gba.ReadMem16(0x04000130);
                ss << "<b>PC:</b> 0x" << std::hex << std::setfill('0') << std::setw(8) << gba.GetPC() << "<br>";
                ss << "<b>Input:</b> " << formatInputState(inputState).toStdString() << "<br>";
                ss << "<b>VCount:</b> " << std::dec << gba.ReadMem16(0x04000006);
            } else if (currentEmulator == EmulatorType::Switch) {
                ss << switchEmulator.GetDebugInfo();
            }
            
            devPanelLabel->setText(QString::fromStdString(ss.str()));
        }

        if (currentEmulator == EmulatorType::GBA) {
            // Run a batch of instructions per frame
            // GBA CPU is 16.78 MHz. 16.78M / 60 ~= 280,000 cycles per frame.
            int cycles = 0;
            while (cycles < 280000) {
                cycles += gba.Step();
            }
            
            // Update Display
            const auto& buffer = gba.GetPPU().GetFramebuffer();
            for (int y = 0; y < 160; ++y) {
                for (int x = 0; x < 240; ++x) {
                    displayImage.setPixel(x, y, buffer[y * 240 + x]);
                }
            }
        } else if (currentEmulator == EmulatorType::Switch) {
            switchEmulator.RunFrame();
            
            // Update Display
            auto* gpu = switchEmulator.GetGPU();
            if (gpu) {
                const auto& buffer = gpu->GetFramebuffer();
                // Buffer is 1280x720 uint32_t (RGBA)
                if (buffer.size() >= 1280 * 720) {
                    // Direct copy if format matches (ARGB32 expects 0xAARRGGBB)
                    // Switch buffer is likely RGBA or ABGR. Let's assume ARGB for now or fix later.
                    // Actually QImage::Format_ARGB32 expects 0xAARRGGBB.
                    // If buffer is raw bytes, we might need conversion.
                    // Assuming buffer is vector<uint32_t>
                    memcpy(displayImage.bits(), buffer.data(), buffer.size() * sizeof(uint32_t));
                }
            }
        }

        // Scale up for visibility
        displayLabel->setPixmap(QPixmap::fromImage(displayImage).scaled(displayLabel->size(), Qt::KeepAspectRatio));
    }

    void MainWindow::keyPressEvent(QKeyEvent *event) {
        if (Input::InputManager::instance().processKeyEvent(event)) {
            // Input handled
        } else {
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
        title->setStyleSheet("font-size: 32px; font-weight: bold; color: #00aaff; margin-bottom: 40px; letter-spacing: 2px;");
        layout->addWidget(title);

        QPushButton *emuBtn = new QPushButton("EMULATORS", mainMenuPage);
        emuBtn->setCursor(Qt::PointingHandCursor);
        connect(emuBtn, &QPushButton::clicked, this, &MainWindow::goToEmulatorSelect);
        layout->addWidget(emuBtn);

        QPushButton *settingsBtn = new QPushButton("SETTINGS", mainMenuPage);
        settingsBtn->setCursor(Qt::PointingHandCursor);
        connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::goToSettings);
        layout->addWidget(settingsBtn);

        layout->addStretch();
        
        QLabel *footer = new QLabel("v1.0.0 | System Ready", mainMenuPage);
        footer->setAlignment(Qt::AlignCenter);
        footer->setStyleSheet("color: #666; font-size: 12px;");
        layout->addWidget(footer);
    }

    void MainWindow::setupSettingsPage() {
        settingsPage = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(settingsPage);
        layout->setContentsMargins(50, 50, 50, 50);

        QLabel *title = new QLabel("SYSTEM SETTINGS", settingsPage);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("font-size: 24px; font-weight: bold; margin-bottom: 30px; color: #aaaaaa;");
        layout->addWidget(title);

        // ROM Directory Setting
        QGroupBox *romGroup = new QGroupBox("ROM Library Path", settingsPage);
        QVBoxLayout *romLayout = new QVBoxLayout(romGroup);
        
        romPathLabel = new QLabel(romDirectory, romGroup);
        romPathLabel->setWordWrap(true);
        romPathLabel->setStyleSheet("border: 1px solid #444; padding: 10px; background-color: #1a1a1a; border-radius: 4px; color: #00ff00; font-family: monospace;");
        romLayout->addWidget(romPathLabel);

        QPushButton *browseBtn = new QPushButton("BROWSE FOLDER...", romGroup);
        browseBtn->setCursor(Qt::PointingHandCursor);
        connect(browseBtn, &QPushButton::clicked, this, &MainWindow::selectRomDirectory);
        romLayout->addWidget(browseBtn);

        layout->addWidget(romGroup);

        layout->addStretch();

        QPushButton *backBtn = new QPushButton("BACK TO MENU", settingsPage);
        backBtn->setCursor(Qt::PointingHandCursor);
        backBtn->setStyleSheet("background-color: #444;"); // Slightly different for back button
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
        title->setStyleSheet("font-size: 24px; font-weight: bold; margin-bottom: 30px; color: #aaaaaa;");
        layout->addWidget(title);

        QPushButton *gbaBtn = new QPushButton("GAME BOY ADVANCE", emulatorSelectPage);
        gbaBtn->setCursor(Qt::PointingHandCursor);
        gbaBtn->setStyleSheet("text-align: left; padding-left: 30px; border-left: 5px solid #6a0dad;"); 
        connect(gbaBtn, &QPushButton::clicked, this, [this]() {
            currentEmulator = EmulatorType::GBA;
            goToGameSelect();
        });
        layout->addWidget(gbaBtn);

        QPushButton *switchBtn = new QPushButton("NINTENDO SWITCH", emulatorSelectPage);
        switchBtn->setCursor(Qt::PointingHandCursor);
        switchBtn->setStyleSheet("text-align: left; padding-left: 30px; border-left: 5px solid #e60012;");
        connect(switchBtn, &QPushButton::clicked, this, [this]() {
            currentEmulator = EmulatorType::Switch;
            goToGameSelect();
        });
        layout->addWidget(switchBtn);

        layout->addStretch();

        QPushButton *backBtn = new QPushButton("BACK", emulatorSelectPage);
        backBtn->setCursor(Qt::PointingHandCursor);
        backBtn->setStyleSheet("background-color: #444;");
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
        gameTimer->stop();
        // Maybe reset emulator?
        // gba.Reset(); 
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
        title->setStyleSheet("font-size: 24px; font-weight: bold; margin-bottom: 20px; color: #aaaaaa;");
        layout->addWidget(title);

        gameListWidget = new QListWidget(gameSelectPage);
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
        backBtn->setStyleSheet("background-color: #444;");
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
        topBar->setStyleSheet("background-color: #1a1a1a; border-bottom: 1px solid #333;");
        topBar->setFixedHeight(40);
        QHBoxLayout *topLayout = new QHBoxLayout(topBar);
        topLayout->setContentsMargins(10, 0, 10, 0);

        QPushButton *stopBtn = new QPushButton("STOP", topBar);
        stopBtn->setFixedSize(80, 30);
        stopBtn->setStyleSheet("font-size: 12px; padding: 5px; min-height: 0;");
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stopGame);
        topLayout->addWidget(stopBtn);

        statusLabel = new QLabel("Ready", topBar);
        statusLabel->setStyleSheet("color: #888; font-size: 12px;");
        topLayout->addWidget(statusLabel);
        
        topLayout->addStretch();

        QPushButton *devBtn = new QPushButton("DEV", topBar);
        devBtn->setCheckable(true);
        devBtn->setFixedSize(60, 30);
        devBtn->setStyleSheet("font-size: 12px; padding: 5px; min-height: 0;");
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
        displayLabel->setStyleSheet("background-color: #000;");
        displayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        gameLayout->addWidget(displayLabel);

        // Dev Panel (Overlay or Side)
        devPanelLabel = new QLabel(gameArea);
        devPanelLabel->setStyleSheet("background-color: rgba(0, 0, 0, 0.8); color: #0f0; font-family: monospace; padding: 10px; border-left: 1px solid #333;");
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


}
