#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QStackedWidget>
#include <QPushButton>
#include <QListWidget>
#include <QSettings>
#include <SDL2/SDL.h>
#include <string>

class QKeyEvent;

#include "emulator/gba/GBA.h"
#include "emulator/switch/SwitchEmulator.h"

namespace AIO {
namespace GUI {

class MainWindow : public QMainWindow {
    Q_OBJECT

    public:
        MainWindow(QWidget *parent = nullptr);
        ~MainWindow();

        void LoadROM(const std::string& path);

    protected:
        void keyPressEvent(QKeyEvent *event) override;
        void keyReleaseEvent(QKeyEvent *event) override;

    private slots:
        void GameLoop();
        void toggleDevPanel(bool enabled);
        
        // Navigation Slots
        void goToMainMenu();
        void goToEmulatorSelect();
        void goToGameSelect();
        void goToSettings();
        void selectRomDirectory();
        void startGame(const QString& path);
        void stopGame();

    private:
        QString formatInputState(uint16_t state);
        void initAudio();
        void closeAudio();
        static void audioCallback(void* userdata, Uint8* stream, int len);
        
        // UI Setup
        void setupMainMenu();
        void setupEmulatorSelect();
        void setupGameSelect();
        void setupEmulatorView();
        void setupSettingsPage();
        
        void loadSettings();
        void saveSettings();
        void refreshGameList();

        // Widgets
        QStackedWidget *stackedWidget;
        QWidget *mainMenuPage;
        QWidget *emulatorSelectPage;
        QWidget *gameSelectPage;
        QWidget *emulatorPage;
        QWidget *settingsPage;
        
        QListWidget *gameListWidget;
        QLabel *romPathLabel;

        // Emulator View Widgets
        QLabel *statusLabel;
        QLabel *displayLabel;
        QLabel *devPanelLabel;
        QCheckBox *devPanelCheckbox;
        
        enum class EmulatorType {
            None,
            GBA,
            Switch
        };
        EmulatorType currentEmulator = EmulatorType::None;

        AIO::Emulator::GBA::GBA gba;
        AIO::Emulator::Switch::SwitchEmulator switchEmulator;
        
        QTimer *gameTimer;
        QImage displayImage;
        uint16_t keyInputState = 0x03FF; // Default: All keys released (1)
        
        // Settings
        QSettings settings;
        QString romDirectory;

        // FPS tracking
        QElapsedTimer fpsTimer;
        int frameCount = 0;
        double currentFPS = 0.0;
        
        // SDL Audio
        SDL_AudioDeviceID audioDevice = 0;
        static constexpr int AUDIO_SAMPLE_RATE = 32768;
        static constexpr int AUDIO_BUFFER_SIZE = 1024;
};

} // namespace GUI
} // namespace AIO

