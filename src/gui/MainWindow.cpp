#include <QApplication>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>
#include <QVBoxLayout>

#include <SDL2/SDL.h>
#include <SDL2/SDL_hints.h>

#include <iostream>

#include "emulator/gba/ARM7TDMI.h"

#include "common/AssetPaths.h"
#include "gui/EmulatorSelectAdapter.h"
#include "gui/GameSelectAdapter.h"
#include "gui/LogViewerDialog.h"
#include "gui/MainMenuAdapter.h"
#include "gui/MainWindow.h"
#include "gui/NASAdapter.h"
#include "gui/SettingsMenuAdapter.h"

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


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), settings("AIOServer", "GBAEmulator")
{
    // Register crash callback for GUI mode.
    AIO::Emulator::GBA::CrashPopupCallback = &ShowCrashPopup;

    // Try to keep controller "Home/Guide" button handling inside the app.
    // Note: some OS-level shortcuts on macOS may still be handled by the OS.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

    // Load unified 10-foot UI theme.
    QString styleSheet;
    QFile f(AIO::Common::AssetPath("qss/tv.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        styleSheet = ts.readAll();
        f.close();
    }

    if (!styleSheet.isEmpty()) {
        setStyleSheet(styleSheet);
    }

    // NOTE: QtWebEngine + macOS has shown crashes in QApplication::notify when
    // app-wide event filters are installed. We only install our global filter
    // when streaming (WebEngine) is disabled.
    loadSettings();
    stackedWidget = new QStackedWidget(this);
    setCentralWidget(stackedWidget);
    // Ensure key events have a stable focus target.
    stackedWidget->setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(stackedWidget);
    setupMainMenu();
    setupEmulatorSelect();
    setupGameSelect();
    setupEmulatorView();
    setupSettingsPage();
    setupNASPage();
    // QtWebEngine on macOS can assert inside AppKit when the app isn't running
    // from a proper .app bundle (mainBundlePath == nil). That crash happens
    // before we can validate navigation/input. Keep streaming disabled unless
    // explicitly enabled.
    streamingEnabled_ = (qEnvironmentVariableIntValue("AIO_ENABLE_STREAMING") == 1);
    if (streamingEnabled_) {
        setupStreamingPages();
    } else {
        streamingHubPage = new QWidget(this);
        auto* layout = new QVBoxLayout(streamingHubPage);
        auto* label = new QLabel("Streaming disabled (set AIO_ENABLE_STREAMING=1)", streamingHubPage);
        label->setAlignment(Qt::AlignCenter);
        auto* backBtn = new QPushButton("Back", streamingHubPage);
        backBtn->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(label);
        layout->addWidget(backBtn);
        layout->setAlignment(backBtn, Qt::AlignHCenter);
        connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);

        // Placeholders so stackedWidget indices remain valid.
        youTubeBrowsePage = new QWidget(this);
        youTubePlayerPage = new QWidget(this);
        streamingWebPage = new QWidget(this);
    }
    stackedWidget->addWidget(mainMenuPage);
    stackedWidget->addWidget(emulatorSelectPage);
    stackedWidget->addWidget(gameSelectPage);
    stackedWidget->addWidget(emulatorPage);
    stackedWidget->addWidget(settingsPage);
    stackedWidget->addWidget(streamingHubPage);
    stackedWidget->addWidget(youTubeBrowsePage);
    stackedWidget->addWidget(youTubePlayerPage);
    stackedWidget->addWidget(streamingWebPage);
    stackedWidget->addWidget(nasPage);
    stackedWidget->setCurrentWidget(mainMenuPage);

    // Keep focus on the currently visible page by default.
    QObject::connect(stackedWidget, &QStackedWidget::currentChanged, this, [this](int) {
        QWidget* current = stackedWidget ? stackedWidget->currentWidget() : nullptr;
        if (!current) return;
        current->setFocusPolicy(Qt::StrongFocus);
        if (!QApplication::focusWidget() || !current->isAncestorOf(QApplication::focusWidget())) {
            current->setFocus(Qt::OtherFocusReason);
        }
    });

    // Ensure initial focus is on the first actionable item.
    QTimer::singleShot(0, this, [this]() {
        if (!mainMenuPage) return;
        if (auto* btn = mainMenuPage->findChild<QPushButton*>()) {
            btn->setFocus();
        } else {
            mainMenuPage->setFocus();
        }
    });
    
    // Display update timer: starts when a game starts.
    displayTimer = new QTimer(this);
    connect(displayTimer, &QTimer::timeout, this, &MainWindow::UpdateDisplay);

    setupNavigation();
    
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

} // namespace GUI
} // namespace AIO
