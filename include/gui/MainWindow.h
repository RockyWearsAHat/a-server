#pragma once

#include <QCheckBox>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <SDL2/SDL.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <termios.h>
#include <thread>
#include <vector>

#include "common/AudioRecorder.h"
#include "common/VideoRecorder.h"
#include "gui/NavigationController.h"
#include "gui/UIActionMapper.h"

namespace AIO::GUI {
class MainMenuAdapter;
class EmulatorSelectAdapter;
class GameSelectAdapter;
class GamesLibraryPage;
class GameStorePage;
class SteamService;
class EmulatorSettingsAdapter;
class SettingsMenuAdapter;
class NASAdapter;
class HomeScreen;
} // namespace AIO::GUI

class QKeyEvent;
class QProcess;

namespace AIO::GUI {
class RemoteControlServer;
}

// Forward declarations break the emulator→GUI include chain.
// Full headers are only needed in the .cpp files that call emulator methods.
namespace AIO::Emulator::GBA {
class GBA;
}
namespace AIO::Emulator::PS1 {
class PS1;
}
namespace AIO::Emulator::Switch {
class SwitchEmulator;
}
namespace AIO::Emulator::Windows {
class WindowsEmulator;
}

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
 * - This class is split across multiple translation units under
 * `src/gui/mainwindow/` to keep each area focused (navigation, pages,
 * emulation, input/audio).
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  /** @brief Construct the main window and initialize pages + input/audio. */
  MainWindow(QWidget *parent = nullptr);

  /** @brief Stop emulation and release resources. */
  ~MainWindow();

  void LoadROM(const std::string &path);
  void SetEmulatorType(int type); // 0=GBA, 1=Switch, 2=PS1, 3=Windows
  // Debugger controls via GUI/CLI
  void EnableDebugger(bool enabled);
  void AddBreakpoint(uint32_t addr);

  // Optional: deterministic playback of input events for debugging.
  // Script format: <time_ms> <KEY> <DOWN|UP>
  // Example line: 1500 A DOWN
  void SetInputScriptPath(const std::string &path);

  // Headless/regression tooling.
  // Writes the most recently rendered frame to a binary PPM (P6).
  // Must be called on the Qt thread.
  bool DumpCurrentFramePPM(const std::string &path,
                           double *outNonBlackRatio = nullptr) const;

  // Audio recording for development/testing.
  // Records SDL audio output directly to a WAV file.
  bool StartAudioRecording(const std::string &path);
  bool StopAudioRecording();
  bool IsAudioRecording() const;
  std::string GetAudioRecordingPath() const;
  double GetAudioRecordingDuration() const;
  AIO::Common::AudioRecorder::AudioMetrics GetAudioMetrics() const;

  // Combined A/V recording for development/testing.
  // Records framebuffer + audio directly from emulator buffers.
  // Perfect sync, cross-platform, no screen capture dependencies.
  bool StartAVRecording(const std::string &path);
  bool StopAVRecording();
  bool IsAVRecording() const;
  uint64_t GetEmulatedMilliseconds() const;

  struct ScriptEvent {
    int64_t ms = 0;
    uint16_t mask = 0;
    bool down = false;
  };

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  bool event(QEvent *e) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

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

  /** @brief Navigate to the emulator settings menu (while a game is running).
   */
  void goToEmulatorSettings();

  /** @brief Close emulator settings and resume the emulator view. */
  void closeEmulatorSettings();

  /** @brief Navigate to the NAS page. */
  void goToNAS();

  /** @brief Navigate to the screen mirror page. */
  void goToScreenMirror();

  /** @brief Navigate to the unified games library page. */
  void goToGamesLibrary();

  /** @brief Launch a locally installed game or app by path. */
  void launchInstalledGame(const QString &path);

  /** @brief Navigate to the game store page. */
  void goToGameStore();

  /** @brief Launch a Steam game by app ID. */
  void launchSteamGame(const QString &steamAppId);

private slots:
  /** @brief UI refresh tick for the emulator framebuffer/status. */
  void UpdateDisplay();
  void toggleDevPanel(bool enabled);

  // Other slots
  void openStreaming();
  void launchStreamingApp(int app);
  void selectRomDirectory();

  /** @brief Start emulation for the given ROM path. */
  void startGame(const QString &path);

  /** @brief Stop emulation and return UI to a safe idle state. */
  void stopGame();

  /** @brief Stop emulation and return to the main menu (homescreen). */
  void stopGameToHome();

private:
  QString formatInputState(uint16_t state);
  void initAudio();
  void closeAudio();
  static void audioCallback(void *userdata, Uint8 *stream, int len);

  // Emulator thread management
  void StartEmulatorThread();
  void StopEmulatorThread();
  void EmulatorThreadMain();

  // UI Setup
  void setupMainMenu();
  void setupHomeScreen();
  void setupEmulatorSelect();
  void setupGameSelect();
  void setupEmulatorView();
  void setupEmulatorSettingsPage();
  void setupSettingsPage();
  void setupStreamingPages();
  void setupNASPage();
  void setupScreenMirrorPage();
  void setupGamesLibraryPage();
  void setupGameStorePage();
  void maybeAutostartServer();
  void stopAutostartedServer();

  void loadSettings();
  void saveSettings();
  void refreshGameList();

  // Global navigation (state-driven)
  void setupNavigation();
  void onPageChanged();
  void onUIAction(const AIO::GUI::UIActionFrame &frame);

  std::unique_ptr<AIO::GUI::MainMenuAdapter> mainMenuAdapter;
  AIO::GUI::HomeScreen *homeScreen_ = nullptr;
  std::unique_ptr<AIO::GUI::EmulatorSelectAdapter> emulatorSelectAdapter;
  std::unique_ptr<AIO::GUI::GameSelectAdapter> gameSelectAdapter;
  std::unique_ptr<AIO::GUI::EmulatorSettingsAdapter> emulatorSettingsAdapter;
  std::unique_ptr<AIO::GUI::SettingsMenuAdapter> settingsMenuAdapter;
  std::unique_ptr<AIO::GUI::NASAdapter> nasAdapter;
  AIO::GUI::NavigationController nav;
  AIO::GUI::UIActionMapper actionMapper;
  QTimer *navTimer = nullptr;

  // Widgets
  QStackedWidget *stackedWidget;
  QWidget *mainMenuPage;
  QWidget *homeScreenPage = nullptr;
  QWidget *emulatorSelectPage;
  QWidget *gameSelectPage;
  QWidget *emulatorPage;
  QWidget *emulatorSettingsPage;
  QWidget *settingsPage;
  QWidget *youTubeBrowsePage;
  QWidget *youTubePlayerPage;
  QWidget *streamingWebPage;
  QWidget *nasPage;
  QWidget *screenMirrorPage_ = nullptr;
  AIO::GUI::GamesLibraryPage *gamesLibraryPage_ = nullptr;
  AIO::GUI::GameStorePage *gameStorePage_ = nullptr;
  AIO::GUI::SteamService *steamService_ = nullptr;

  QListWidget *gameListWidget;
  QLabel *romPathLabel;

  // Emulator settings UI state
  QLabel *emuSettingsStatusLabel_ = nullptr;
  bool emuSettingsCapturingRebind_ = false;
  AIO::Input::LogicalButton emuSettingsCaptureLogical_ =
      AIO::Input::LogicalButton::Confirm;

  // Emulator View Widgets
  QLabel *statusLabel;
  QLabel *displayLabel;
  QLabel *devPanelLabel;
  QCheckBox *devPanelCheckbox;

  enum class EmulatorType { None, GBA, Switch, PS1, Windows };
  EmulatorType currentEmulator = EmulatorType::None;

  std::unique_ptr<AIO::Emulator::GBA::GBA> gba;
  std::unique_ptr<AIO::Emulator::Switch::SwitchEmulator> switchEmulator;
  std::unique_ptr<AIO::Emulator::PS1::PS1> ps1Emulator;
  std::unique_ptr<AIO::Emulator::Windows::WindowsEmulator> windowsEmulator;

  QTimer *displayTimer; // UI update timer (60 Hz)
  QImage displayImage;
  uint16_t keyInputState = 0x03FF; // Default: All keys released (1)

  // Settings
  QSettings settings;
  QString romDirectory;

  // Video scaling (emulator output)
  enum class VideoScaleMode {
    IntegerNearest = 0,
    FitNearest = 1,
  };
  VideoScaleMode videoScaleMode_ = VideoScaleMode::IntegerNearest;
  int videoIntegerScale_ = 0; // 0 = auto

  QImage scaledDisplayImage_;

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
  static constexpr int AUDIO_BUFFER_SIZE = 512;

  // Audio recording for development/testing
  AIO::Common::AudioRecorder audioRecorder_;

  // Combined A/V recording for development/testing
  AIO::Common::AVRecorder avRecorder_;

  // Emulator thread
  std::thread emulatorThread;
  std::atomic<bool> emulatorRunning{false};
  std::atomic<bool> emulatorPaused{false};
  std::atomic<bool> emulatorStepOne{false};
  std::atomic<int> emulatorStepBack{0};
  std::atomic<uint64_t> emulatorFrameNumber{0};
  std::mutex emulatorStateMutex;

  // Frame history for step-back (simple serialized states)
  // Fixed-size arrays match GBA hardware memory map exactly,
  // avoiding per-frame heap allocation (~536KB * 60fps = 32MB/s).
  struct FrameSnapshot {
    static constexpr size_t IWRAM_SIZE = 0x8000;  // 32KB
    static constexpr size_t EWRAM_SIZE = 0x40000; // 256KB
    static constexpr size_t VRAM_SIZE = 0x18000;  // 96KB
    static constexpr size_t OAM_SIZE = 0x400;     // 1KB
    static constexpr size_t PALETTE_SIZE = 0x400; // 1KB
    static constexpr size_t IO_SIZE = 0x400;      // 1KB
    static constexpr size_t FB_SIZE = 240 * 160;  // 38400 pixels

    std::array<uint8_t, IWRAM_SIZE> iwram;
    std::array<uint8_t, EWRAM_SIZE> ewram;
    std::array<uint8_t, VRAM_SIZE> vram;
    std::array<uint8_t, OAM_SIZE> oam;
    std::array<uint8_t, PALETTE_SIZE> palette;
    std::array<uint8_t, IO_SIZE> ioRegs;
    std::array<uint32_t, FB_SIZE> framebuffer;
    uint32_t cpuRegisters[16]{};
    uint32_t cpsr = 0;
    uint64_t frameNum = 0;
  };
  // Heap-allocated to avoid blowing the stack (~53MB total)
  std::unique_ptr<std::array<FrameSnapshot, 100>> frameHistory =
      std::make_unique<std::array<FrameSnapshot, 100>>();
  static constexpr size_t MAX_FRAME_HISTORY = 100;
  size_t frameHistoryIndex = 0;
  size_t frameHistoryWritten = 0;

  // Debugger flags
  bool debuggerEnabled = false;
  bool debuggerContinue = false;
  bool stdinRawEnabled = false;
  struct termios rawTermios;

  // Input mode tracking
  enum class InputMode { Mouse, Controller };
  InputMode currentInputMode = InputMode::Mouse;

  // Mouse hover tracking (for sticky hover until mouse leaves)
  QPushButton *lastHoveredButton = nullptr;

  // Cache this once at startup; used to avoid global event filter issues with
  // QtWebEngine.
  bool streamingEnabled_ = false;
  std::vector<ScriptEvent> inputScript_;
  size_t nextScriptEvent_ = 0;
  uint16_t scriptKeyState_ = 0x03FF;
  QElapsedTimer scriptTimer_;
  std::atomic<bool> scriptEnabled_{false};
  QString inputScriptPath_;

  // Published by UI/input polling; consumed/applied by emulation thread.
  // 0x03FF = all released (GBA KEYINPUT is active-low).
  std::atomic<uint16_t> pendingEmuKeyinput{0x03FF};
  QProcess *serverProcess_ = nullptr;

  // Localhost HTTP server for programmatic input injection (visual dev loop)
  friend class RemoteControlServer;

  QWidget *nowPlayingOverlay_ = nullptr;
};

} // namespace GUI
} // namespace AIO
