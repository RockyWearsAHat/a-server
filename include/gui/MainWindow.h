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
#include <termios.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

#include "gui/NavigationController.h"
#include "gui/UIActionMapper.h"

namespace AIO::GUI {
class MainMenuAdapter;
class EmulatorSelectAdapter;
class GameSelectAdapter;
class SettingsMenuAdapter;
class NASAdapter;
}

class QKeyEvent;

#include "emulator/gba/GBA.h"
#include "emulator/switch/SwitchEmulator.h"

namespace AIO {
namespace GUI {

/**
 * @brief Primary Qt Widgets window for the 10-foot UI.
 *
 * Responsibilities:
 * - Hosts the page stack (menus, emulator view, NAS page, optional streaming).
 * - Owns SDL audio output for the emulator APU.
 * - Orchestrates emulator start/stop and the UI refresh loop.
 *
 * Notes:
 * - This class is split across multiple translation units under `src/gui/mainwindow/`
 *   to keep each area focused (navigation, pages, emulation, input/audio).
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

    public:
        /** @brief Construct the main window and initialize pages + input/audio. */
        MainWindow(QWidget *parent = nullptr);

        /** @brief Stop emulation and release resources. */
        ~MainWindow();

        void LoadROM(const std::string& path);
        void SetEmulatorType(int type); // 0=GBA, 1=Switch
        // Debugger controls via GUI/CLI
        void EnableDebugger(bool enabled);
        void AddBreakpoint(uint32_t addr);

    protected:
        void keyPressEvent(QKeyEvent *event) override;
        void keyReleaseEvent(QKeyEvent *event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        bool event(QEvent* e) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

    public slots:
        // Navigation - public so adapters can call them
        /** @brief Navigate to the main menu page. */
        void goToMainMenu();

        /** @brief Navigate to emulator selection. */
        void goToEmulatorSelect();

        /** @brief Navigate to ROM/game selection. */
        void goToGameSelect();

        /** @brief Navigate to settings. */
        void goToSettings();

        /** @brief Navigate to the NAS page. */
        void goToNAS();

    private slots:
        /** @brief UI refresh tick for the emulator framebuffer/status. */
        void UpdateDisplay();
        void toggleDevPanel(bool enabled);
        
        // Other slots
        void openStreaming();
        void launchStreamingApp(int app);
        void selectRomDirectory();

        /** @brief Start emulation for the given ROM path. */
        void startGame(const QString& path);

        /** @brief Stop emulation and return UI to a safe idle state. */
        void stopGame();

    private:
        QString formatInputState(uint16_t state);
        void initAudio();
        void closeAudio();
        static void audioCallback(void* userdata, Uint8* stream, int len);
        
        // Emulator thread management
        void StartEmulatorThread();
        void StopEmulatorThread();
        void EmulatorThreadMain();
        
        // UI Setup
        void setupMainMenu();
        void setupEmulatorSelect();
        void setupGameSelect();
        void setupEmulatorView();
        void setupSettingsPage();
        void setupStreamingPages();
        void setupNASPage();
        
        void loadSettings();
        void saveSettings();
        void refreshGameList();

        // Global navigation (state-driven)
        void setupNavigation();
        void onPageChanged();
        void onUIAction(const AIO::GUI::UIActionFrame& frame);

        std::unique_ptr<AIO::GUI::MainMenuAdapter> mainMenuAdapter;
        std::unique_ptr<AIO::GUI::EmulatorSelectAdapter> emulatorSelectAdapter;
        std::unique_ptr<AIO::GUI::GameSelectAdapter> gameSelectAdapter;
        std::unique_ptr<AIO::GUI::SettingsMenuAdapter> settingsMenuAdapter;
        std::unique_ptr<AIO::GUI::NASAdapter> nasAdapter;
        AIO::GUI::NavigationController nav;
        AIO::GUI::UIActionMapper actionMapper;
        QTimer* navTimer = nullptr;

        // Widgets
        QStackedWidget *stackedWidget;
        QWidget *mainMenuPage;
        QWidget *emulatorSelectPage;
        QWidget *gameSelectPage;
        QWidget *emulatorPage;
        QWidget *settingsPage;
        QWidget *streamingHubPage;
        QWidget *youTubeBrowsePage;
        QWidget *youTubePlayerPage;
        QWidget *streamingWebPage;
        QWidget *nasPage;
        
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
        
        QTimer *displayTimer;  // UI update timer (60 Hz)
        QImage displayImage;
        uint16_t keyInputState = 0x03FF; // Default: All keys released (1)
        
        // Settings
        QSettings settings;
        QString romDirectory;

        // FPS tracking
        QElapsedTimer fpsTimer;
        int frameCount = 0;
        double currentFPS = 0.0;
        
        // Periodic save flushing (every 60 frames = 1 second at 60 FPS)
        int saveFlushCounter = 0;
        static constexpr int SAVE_FLUSH_INTERVAL = 60;
        
        
        // SDL Audio
        SDL_AudioDeviceID audioDevice = 0;
        static constexpr int AUDIO_SAMPLE_RATE = 32768;
        static constexpr int AUDIO_BUFFER_SIZE = 1024;
        
        // Emulator thread
        std::thread emulatorThread;
        std::atomic<bool> emulatorRunning{false};
        std::atomic<bool> emulatorPaused{false};
        std::mutex emulatorStateMutex;

        // Debugger flags
        bool debuggerEnabled = false;
        bool debuggerContinue = false;
        bool stdinRawEnabled = false;
        struct termios rawTermios;
        
        // Input mode tracking
        enum class InputMode { Mouse, Controller };
        InputMode currentInputMode = InputMode::Mouse;
        
        // Mouse hover tracking (for sticky hover until mouse leaves)
        QPushButton* lastHoveredButton = nullptr;

        // Cache this once at startup; used to avoid global event filter issues with QtWebEngine.
        bool streamingEnabled_ = false;
};

} // namespace GUI
} // namespace AIO


