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

struct YouTubeServerAutobootConfig {
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

static bool WaitForYouTubeServerReady(int port, int timeoutMs) {
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

static std::optional<YouTubeServerAutobootConfig>
ResolveYouTubeServerAutobootConfig() {
  const QByteArray enabled = qgetenv("AIO_YOUTUBE_SERVER_AUTOBOOT");
  if (enabled.isEmpty() || enabled == "0") {
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

  return YouTubeServerAutobootConfig{nodeExecutable, workDir, entryPath, port};
}

namespace AIO {
namespace GUI {

void MainWindow::maybeAutostartYouTubeServer() {
  if (youtubeServerProcess_) {
    return;
  }

  const auto config = ResolveYouTubeServerAutobootConfig();
  if (!config.has_value()) {
    return;
  }

  youtubeServerProcess_ = new QProcess(this);
  youtubeServerProcess_->setProgram(config->nodeExecutable);
  youtubeServerProcess_->setArguments({config->entryPath});
  youtubeServerProcess_->setWorkingDirectory(config->workDir);
  youtubeServerProcess_->setProcessChannelMode(QProcess::ForwardedChannels);
  youtubeServerProcess_->start();

  if (!youtubeServerProcess_->waitForStarted(5000)) {
    std::cerr << "[YouTube] Failed to autostart server from "
              << config->entryPath.toStdString() << std::endl;
    youtubeServerProcess_->deleteLater();
    youtubeServerProcess_ = nullptr;
    return;
  }

  if (!WaitForYouTubeServerReady(config->port, 6000)) {
    std::cerr << "[YouTube] Autostarted server did not become healthy on port "
              << config->port << std::endl;
  }

  std::cout << "[YouTube] Autostarted local YouTube server on port "
            << config->port << std::endl;
}

void MainWindow::stopAutostartedYouTubeServer() {
  if (!youtubeServerProcess_) {
    return;
  }

  if (youtubeServerProcess_->state() != QProcess::NotRunning) {
    youtubeServerProcess_->terminate();
    if (!youtubeServerProcess_->waitForFinished(3000)) {
      youtubeServerProcess_->kill();
      youtubeServerProcess_->waitForFinished(2000);
    }
  }

  youtubeServerProcess_->deleteLater();
  youtubeServerProcess_ = nullptr;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), settings("AIOServer", "GBAEmulator"),
      gba(std::make_unique<AIO::Emulator::GBA::GBA>()),
      switchEmulator(std::make_unique<AIO::Emulator::Switch::SwitchEmulator>()),
      ps1Emulator(std::make_unique<AIO::Emulator::PS1::PS1>()) {
  maybeAutostartYouTubeServer();
  QObject::connect(qApp, &QCoreApplication::aboutToQuit, this,
                   [this]() { stopAutostartedYouTubeServer(); });

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
  setupEmulatorSettingsPage();
  setupSettingsPage();
  setupNASPage();
  // Streaming is enabled by default. Keep a kill switch so WebEngine can be
  // disabled quickly on machines with driver/platform issues.
  streamingEnabled_ =
      (qEnvironmentVariableIntValue("AIO_DISABLE_STREAMING") == 0);
  if (streamingEnabled_) {
    setupStreamingPages();
  } else {
    streamingHubPage = new QWidget(this);
    auto *layout = new QVBoxLayout(streamingHubPage);
    auto *label = new QLabel("Streaming disabled by AIO_DISABLE_STREAMING=1",
                             streamingHubPage);
    label->setAlignment(Qt::AlignCenter);
    auto *backBtn = new QPushButton("Back", streamingHubPage);
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
  stackedWidget->addWidget(emulatorSettingsPage);
  stackedWidget->addWidget(settingsPage);
  stackedWidget->addWidget(streamingHubPage);
  stackedWidget->addWidget(youTubeBrowsePage);
  stackedWidget->addWidget(youTubePlayerPage);
  stackedWidget->addWidget(streamingWebPage);
  stackedWidget->addWidget(nasPage);
  stackedWidget->setCurrentWidget(mainMenuPage);

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
    if (!mainMenuPage)
      return;
    if (auto *btn = mainMenuPage->findChild<QPushButton *>()) {
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
}

MainWindow::~MainWindow() {
  stopAutostartedYouTubeServer();
  StopEmulatorThread();
  closeAudio();
}

} // namespace GUI
} // namespace AIO
