#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include <atomic>

#include <iostream>

#include <SDL2/SDL.h>
#include <SDL2/SDL_hints.h>

#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBA.h"
#include "emulator/ps1/PS1.h"
#include "emulator/switch/SwitchEmulator.h"

#include "common/AssetPaths.h"
#include "gui/EmulatorSelectAdapter.h"
#include "gui/EmulatorSettingsAdapter.h"
#include "gui/GameSelectAdapter.h"
#include "gui/GameStorePage.h"
#include "gui/GamesLibraryPage.h"
#include "gui/LogViewerDialog.h"
#include "gui/MainMenuAdapter.h"
#include "gui/MainWindow.h"
#include "gui/NASAdapter.h"
#include "gui/SettingsMenuAdapter.h"

// Helper function at global scope to avoid Qt template instantiation issues
static std::atomic_bool crashPopupShown{false};

static void ShowCrashPopup(const char *logPath) {
  bool expected = false;
  if (!crashPopupShown.compare_exchange_strong(expected, true)) {
    return;
  }

  const QString path = QString::fromUtf8(logPath ? logPath : "");
  QCoreApplication *app = QCoreApplication::instance();
  if (!app) {
    return;
  }

  // CrashPopupCallback is invoked from the emulation thread; UI work must run
  // on the Qt main thread (macOS AppKit will abort otherwise).
  if (QThread::currentThread() != app->thread()) {
    QMetaObject::invokeMethod(
        app,
        [path]() {
          ShowCrashPopup(path.isEmpty() ? nullptr : path.toUtf8().constData());
        },
        Qt::QueuedConnection);
    return;
  }

  QMessageBox msg;
  msg.setWindowTitle("Emulator Crash Detected");
  msg.setText("The emulator has crashed. A detailed log has been saved.\nWould "
              "you like to view the log?");
  msg.setIcon(QMessageBox::Critical);
  msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  const int ret = msg.exec();
  if (ret == QMessageBox::Yes && !path.isEmpty()) {
    auto *dlg = new AIO::GUI::LogViewerDialog(nullptr);
    dlg->loadLogFile(path);
    dlg->exec();
    delete dlg;
  }
  QCoreApplication::quit();
}

struct ServerAutobootConfig {
  QString nodeExecutable;
  QString workDir;
  QString entryPath;
  int port = 8916;
};

static QString ResolveLocalServerWorkDir() {
  const QString configured = qEnvironmentVariable("AIO_YOUTUBE_SERVER_WORKDIR");
  if (!configured.isEmpty()) {
    return QFileInfo(configured).absoluteFilePath();
  }

  const QStringList candidates = {
      QDir(QDir::currentPath()).filePath(QStringLiteral("server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../../AIO Server/server")),
  };

  for (const QString &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isDir()) {
      return info.absoluteFilePath();
    }
  }

  return QDir(QDir::currentPath()).filePath(QStringLiteral("server"));
}

static QString ReadEnvValueFromFile(const QString &envPath,
                                    const QString &key) {
  QFile file(envPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QTextStream stream(&file);
  while (!stream.atEnd()) {
    QString line = stream.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }

    const int separator = line.indexOf('=');
    if (separator <= 0) {
      continue;
    }

    if (line.left(separator).trimmed() != key) {
      continue;
    }

    QString value = line.mid(separator + 1).trimmed();
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith('\'') && value.endsWith('\''))) {
      value = value.mid(1, value.size() - 2);
    }
    return value;
  }

  return QString();
}

static int ResolveLocalServerPort(const QString &workDir) {
  const QString envPath = QDir(workDir).filePath(QStringLiteral(".env"));
  bool ok = false;
  const int port =
      ReadEnvValueFromFile(envPath, QStringLiteral("PORT")).toInt(&ok);
  if (ok && port > 0 && port <= 65535) {
    return port;
  }
  return 8916;
}

static bool WaitForServerReady(int port, int timeoutMs) {
  QNetworkAccessManager manager;
  QElapsedTimer elapsed;
  elapsed.start();

  while (elapsed.elapsed() < timeoutMs) {
    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(port)));
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(500);
    loop.exec();

    const bool ok =
        reply->isFinished() && reply->error() == QNetworkReply::NoError &&
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() ==
            200;
    reply->deleteLater();
    if (ok) {
      return true;
    }

    QThread::msleep(150);
  }

  return false;
}

static std::optional<ServerAutobootConfig> ResolveServerAutobootConfig() {
  // If an external server URL is configured, don't start the local one.
  if (!qgetenv("AIO_SERVER_URL").isEmpty()) {
    return std::nullopt;
  }
  // Explicit opt-out.
  if (qgetenv("AIO_YOUTUBE_SERVER_AUTOBOOT") == "0") {
    return std::nullopt;
  }

  const QString nodeExecutable =
      qEnvironmentVariable("AIO_YOUTUBE_SERVER_NODE", QStringLiteral("node"));
  QString workDir = qEnvironmentVariable("AIO_YOUTUBE_SERVER_WORKDIR");
  QString entryScript = qEnvironmentVariable("AIO_YOUTUBE_SERVER_ENTRY");

  if (workDir.isEmpty()) {
    workDir = ResolveLocalServerWorkDir();
  }
  if (entryScript.isEmpty()) {
    entryScript = QStringLiteral("dist/index.js");
  }

  const int port = ResolveLocalServerPort(workDir);

  const QString entryPath =
      QFileInfo(QDir(workDir).filePath(entryScript)).absoluteFilePath();
  if (!QFileInfo::exists(entryPath)) {
    std::cout << "[YouTube] Server autoboot skipped; entry script missing at "
              << entryPath.toStdString() << std::endl;
    return std::nullopt;
  }

  return ServerAutobootConfig{nodeExecutable, workDir, entryPath, port};
}

namespace AIO {
namespace GUI {

void MainWindow::maybeAutostartServer() {
  if (serverProcess_) {
    return;
  }

  const auto config = ResolveServerAutobootConfig();
  if (!config.has_value()) {
    return;
  }

  if (WaitForServerReady(config->port, 1000)) {
    std::cout << "[YouTube] Server already running on port " << config->port
              << std::endl;
    return;
  }

  serverProcess_ = new QProcess(this);
  serverProcess_->setProgram(config->nodeExecutable);
  serverProcess_->setArguments({config->entryPath});
  serverProcess_->setWorkingDirectory(config->workDir);
  serverProcess_->setProcessChannelMode(QProcess::ForwardedChannels);
  serverProcess_->start();

  if (!serverProcess_->waitForStarted(5000)) {
    std::cerr << "[Server] Failed to autostart server from "
              << config->entryPath.toStdString() << std::endl;
    serverProcess_->deleteLater();
    serverProcess_ = nullptr;
    return;
  }

  if (!WaitForServerReady(config->port, 6000)) {
    std::cerr << "[YouTube] Autostarted server did not become healthy on port "
              << config->port << std::endl;
  }

  std::cout << "[Server] Autostarted local server on port " << config->port
            << std::endl;
}

void MainWindow::stopAutostartedServer() {
  if (!serverProcess_) {
    return;
  }

  if (serverProcess_->state() != QProcess::NotRunning) {
    serverProcess_->terminate();
    if (!serverProcess_->waitForFinished(3000)) {
      serverProcess_->kill();
      serverProcess_->waitForFinished(2000);
    }
  }

  serverProcess_->deleteLater();
  serverProcess_ = nullptr;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), settings("AIOServer", "GBAEmulator"),
      gba(std::make_unique<AIO::Emulator::GBA::GBA>()),
      switchEmulator(std::make_unique<AIO::Emulator::Switch::SwitchEmulator>()),
      ps1Emulator(std::make_unique<AIO::Emulator::PS1::PS1>()) {
  maybeAutostartServer();
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, this,
                   [this]() { stopAutostartedServer(); });

  // Register crash callback for GUI mode.
  AIO::Emulator::GBA::CrashPopupCallback = &ShowCrashPopup;

  // Try to keep controller "Home/Guide" button handling inside the app.
  // Note: some OS-level shortcuts on macOS may still be handled by the OS.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

  // Load the global 10-foot theme and append YouTube-specific styling.
  QString styleSheet;
  QStringList loadedStylePaths;
  QStringList missingStylePaths;
  const QStringList stylePaths = {
      AIO::Common::AssetPath("qss/tv.qss"),
      AIO::Common::AssetPath("qss/youtube.qss"),
  };
  for (const QString &path : stylePaths) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      missingStylePaths << path;
      continue;
    }
    QTextStream ts(&f);
    styleSheet += ts.readAll();
    styleSheet += QLatin1Char('\n');
    loadedStylePaths << path;
    f.close();
  }

  if (!styleSheet.isEmpty()) {
    qApp->setStyleSheet(styleSheet);
    std::cout << "[QSS] Loaded " << loadedStylePaths.size()
              << " stylesheet(s) into QApplication" << std::endl;
    for (const QString &path : loadedStylePaths) {
      std::cout << "[QSS]   loaded: " << path.toStdString() << std::endl;
    }
  } else {
    std::cout << "[QSS] No stylesheet content loaded" << std::endl;
  }

  for (const QString &path : missingStylePaths) {
    std::cout << "[QSS]   missing: " << path.toStdString() << std::endl;
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
  setupHomeScreen();
  setupEmulatorSelect();
  setupGameSelect();
  setupEmulatorView();
  setupEmulatorSettingsPage();
  setupSettingsPage();
  setupNASPage();
  setupScreenMirrorPage();
  setupGamesLibraryPage();
  setupGameStorePage();
  // Streaming is enabled by default. Keep a kill switch so WebEngine can be
  // disabled quickly on machines with driver/platform issues.
  streamingEnabled_ =
      (qEnvironmentVariableIntValue("AIO_DISABLE_STREAMING") == 0);
  if (streamingEnabled_) {
    setupStreamingPages();
  } else {
    // Placeholders so stackedWidget indices remain valid.
    youTubeBrowsePage = new QWidget(this);
    youTubePlayerPage = new QWidget(this);
    streamingWebPage = new QWidget(this);
  }
  stackedWidget->addWidget(mainMenuPage);
  stackedWidget->addWidget(homeScreenPage);
  stackedWidget->addWidget(emulatorSelectPage);
  stackedWidget->addWidget(gameSelectPage);
  stackedWidget->addWidget(emulatorPage);
  stackedWidget->addWidget(emulatorSettingsPage);
  stackedWidget->addWidget(settingsPage);
  stackedWidget->addWidget(youTubeBrowsePage);
  stackedWidget->addWidget(youTubePlayerPage);
  stackedWidget->addWidget(streamingWebPage);
  stackedWidget->addWidget(nasPage);
  stackedWidget->addWidget(screenMirrorPage_);
  stackedWidget->addWidget(gamesLibraryPage_);
  stackedWidget->addWidget(gameStorePage_);
  stackedWidget->setCurrentWidget(homeScreenPage);

  // Keep focus on the currently visible page by default.
  QObject::connect(
      stackedWidget, &QStackedWidget::currentChanged, this, [this](int) {
        QWidget *current =
            stackedWidget ? stackedWidget->currentWidget() : nullptr;
        if (!current)
          return;
        current->setFocusPolicy(Qt::StrongFocus);
        if (!QApplication::focusWidget() ||
            !current->isAncestorOf(QApplication::focusWidget())) {
          current->setFocus(Qt::OtherFocusReason);
        }
      });

  // Ensure initial focus is on the first actionable item.
  QTimer::singleShot(0, this, [this]() {
    if (homeScreenPage) {
      homeScreenPage->setFocus();
    } else if (mainMenuPage) {
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
}

MainWindow::~MainWindow() {
  stopAutostartedServer();
  StopEmulatorThread();
  closeAudio();
}

} // namespace GUI
} // namespace AIO
