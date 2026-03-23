#include "gui/MainWindow.h"

#include <QCheckBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "gui/EmulatorSelectAdapter.h"
#include "gui/EmulatorSettingsAdapter.h"
#include "gui/GameSelectAdapter.h"
#include "gui/GameStorePage.h"
#include "gui/GamesLibraryPage.h"
#include "gui/HomeScreen.h"
#include "gui/MainMenuAdapter.h"
#include "gui/NASAdapter.h"
#include "gui/NASPage.h"
#include "gui/ScreenMirrorPage.h"
#include "gui/SettingsMenuAdapter.h"
#include "gui/SteamService.h"
#include "gui/StreamingApp.h"
#include "gui/StreamingWebViewPage.h"
#include "gui/YouTubeBrowsePage.h"
#include "gui/YouTubePlayerPage.h"

#include "input/InputManager.h"

namespace AIO {
namespace GUI {

void MainWindow::setupMainMenu() {
  mainMenuPage = new QWidget(this);
  mainMenuPage->setObjectName("aioMainMenuPage");
  QVBoxLayout *layout = new QVBoxLayout(mainMenuPage);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(20);

  auto makeMenuButton = [&](const QString &text, auto slot) -> QPushButton * {
    auto *b = new QPushButton(text, mainMenuPage);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::StrongFocus);
    b->setProperty("nav_kind", "main");
    connect(b, &QPushButton::clicked, this, slot);
    layout->addWidget(b);
    return b;
  };

  QLabel *title = new QLabel("AIO Entertainment System", mainMenuPage);
  title->setAlignment(Qt::AlignCenter);
  title->setProperty("role", "title");
  layout->addWidget(title);

  QLabel *subtitle = new QLabel(
      "Console-first media and emulation, designed for the living room.",
      mainMenuPage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setProperty("role", "subtitle");
  layout->addWidget(subtitle);

  layout->addSpacing(12);

  QPushButton *emuBtn =
      makeMenuButton("Emulators", &MainWindow::goToEmulatorSelect);
  QPushButton *streamBtn =
      makeMenuButton("Streaming", &MainWindow::openStreaming);
  QPushButton *nasBtn = makeMenuButton("Media Server", &MainWindow::goToNAS);
  QPushButton *settingsBtn =
      makeMenuButton("Settings", &MainWindow::goToSettings);

  layout->addStretch();

  QLabel *footer = new QLabel("v1.0.0", mainMenuPage);
  footer->setAlignment(Qt::AlignCenter);
  footer->setProperty("role", "footer");
  layout->addWidget(footer);

  // State-driven navigation adapter (single unified outline).
  mainMenuAdapter = std::make_unique<AIO::GUI::MainMenuAdapter>(
      this, mainMenuPage,
      std::vector<QPushButton *>{emuBtn, streamBtn, nasBtn, settingsBtn});
}

void MainWindow::setupHomeScreen() {
  auto *home = new HomeScreen(this);
  homeScreenPage = home;
  homeScreen_ = home;

  connect(home, &HomeScreen::gbaRequested, this, &MainWindow::goToGamesLibrary);
  connect(home, &HomeScreen::ps1Requested, this, &MainWindow::goToGamesLibrary);
  connect(home, &HomeScreen::switchRequested, this,
          &MainWindow::goToGamesLibrary);
  connect(home, &HomeScreen::libraryRequested, this,
          &MainWindow::goToGamesLibrary);
  connect(home, &HomeScreen::nasRequested, this, &MainWindow::goToNAS);
  connect(home, &HomeScreen::storeRequested, this, &MainWindow::goToGameStore);
  connect(home, &HomeScreen::screenMirrorRequested, this,
          &MainWindow::goToScreenMirror);
  connect(home, &HomeScreen::settingsRequested, this,
          &MainWindow::goToSettings);
  connect(home, &HomeScreen::streamingAppRequested, this,
          [this](AIO::GUI::StreamingApp app) {
            launchStreamingApp(static_cast<int>(app));
          });
}

void MainWindow::goToNAS() {
  if (!nasPage)
    return;
  stackedWidget->setCurrentWidget(nasPage);
  nasPage->setFocus();
}

void MainWindow::setupNASPage() {
  auto *page = new NASPage(this);
  nasPage = page;
  connect(page, &NASPage::homeRequested, this, &MainWindow::goToMainMenu);

  nasAdapter =
      std::make_unique<AIO::GUI::NASAdapter>(nasPage,
                                             std::vector<QPushButton *>{
                                                 page->upButton(),
                                                 page->refreshButton(),
                                                 page->mkdirButton(),
                                                 page->renameButton(),
                                                 page->deleteButton(),
                                                 page->uploadButton(),
                                                 page->backButton(),
                                             },
                                             page->listWidget());
}

void MainWindow::goToScreenMirror() {
  if (!screenMirrorPage_)
    return;
  stackedWidget->setCurrentWidget(screenMirrorPage_);
  screenMirrorPage_->setFocus();
}

void MainWindow::setupScreenMirrorPage() {
  auto *page = new AIO::GUI::ScreenMirrorPage(this);
  screenMirrorPage_ = page;
  connect(page, &AIO::GUI::ScreenMirrorPage::homeRequested, this,
          &MainWindow::goToMainMenu);
}

void MainWindow::setupGamesLibraryPage() {
  gamesLibraryPage_ = new AIO::GUI::GamesLibraryPage(this);
  connect(gamesLibraryPage_, &AIO::GUI::GamesLibraryPage::backRequested, this,
          &MainWindow::goToMainMenu);
  connect(gamesLibraryPage_, &AIO::GUI::GamesLibraryPage::gameSelected, this,
          &MainWindow::launchInstalledGame);
}

void MainWindow::goToGamesLibrary() {
  if (!gamesLibraryPage_)
    return;
  gamesLibraryPage_->refresh();
  stackedWidget->setCurrentWidget(gamesLibraryPage_);
  gamesLibraryPage_->setFocus();
}

void MainWindow::setupGameStorePage() {
  gameStorePage_ = new AIO::GUI::GameStorePage(this);
  connect(gameStorePage_, &AIO::GUI::GameStorePage::homeRequested, this,
          &MainWindow::goToMainMenu);

  steamService_ = new AIO::GUI::SteamService(this);
  gameStorePage_->setSteamService(steamService_);
  connect(gameStorePage_, &AIO::GUI::GameStorePage::gameSelected, this,
          &MainWindow::launchSteamGame);
  connect(gameStorePage_, &AIO::GUI::GameStorePage::romLaunchRequested, this,
          &MainWindow::launchInstalledGame);
}

void MainWindow::launchInstalledGame(const QString &path) {
  const QString lower = path.toLower();
  if (lower.endsWith(QStringLiteral(".gba"))) {
    currentEmulator = EmulatorType::GBA;
    startGame(path);
    return;
  }
  if (lower.endsWith(QStringLiteral(".bin")) ||
      lower.endsWith(QStringLiteral(".cue")) ||
      lower.endsWith(QStringLiteral(".iso")) ||
      lower.endsWith(QStringLiteral(".img"))) {
    currentEmulator = EmulatorType::PS1;
    startGame(path);
    return;
  }
  if (lower.endsWith(QStringLiteral(".xci")) ||
      lower.endsWith(QStringLiteral(".nsp")) ||
      lower.endsWith(QStringLiteral(".nso")) ||
      lower.endsWith(QStringLiteral(".nro"))) {
    // Switch runtime is intentionally unavailable in production UI.
    return;
  }

  qWarning() << "Unsupported installed game or app launch path:" << path;
}

void MainWindow::goToGameStore() {
  if (!gameStorePage_)
    return;
  stackedWidget->setCurrentWidget(gameStorePage_);
  gameStorePage_->setFocus();
}

void MainWindow::launchSteamGame(const QString &steamAppId) {
  if (!steamService_)
    return;

  steamService_->refreshInstalledGames();
  const int appId = steamAppId.toInt();
  if (appId <= 0)
    return;
  const bool isInstalled = steamService_->isInstalled(appId);
  const QString scheme = isInstalled ? QStringLiteral("steam://run/")
                                     : QStringLiteral("steam://install/");
  const QString steamUrl = scheme + steamAppId;

  if (!QDesktopServices::openUrl(QUrl(steamUrl))) {
    // Steam not running — fall back to opening the store page in the browser
    const QUrl browserUrl(
        QStringLiteral("https://store.steampowered.com/app/") + steamAppId +
        QStringLiteral("/"));
    if (QDesktopServices::openUrl(browserUrl))
      return;
    // Show error overlay if browser also failed
    if (!nowPlayingOverlay_) {
      nowPlayingOverlay_ = new QWidget(this);
      nowPlayingOverlay_->setStyleSheet(
          QStringLiteral("background-color: rgba(0, 0, 0, 0.72);"));
      auto *lay = new QVBoxLayout(nowPlayingOverlay_);
      lay->setAlignment(Qt::AlignCenter);
      auto *msg = new QLabel(
          QStringLiteral("Could not connect to Steam.\nIs Steam running?"),
          nowPlayingOverlay_);
      msg->setAlignment(Qt::AlignCenter);
      msg->setStyleSheet(
          QStringLiteral("color: #f0f0f0; font-size: 16px; font-weight: 500;"));
      lay->addWidget(msg);
    }
    nowPlayingOverlay_->setGeometry(this->rect());
    nowPlayingOverlay_->raise();
    nowPlayingOverlay_->show();
    // Auto-dismiss in 3 seconds
    QTimer::singleShot(3000, nowPlayingOverlay_, &QWidget::hide);
    return;
  }

  // Show "Now Playing" overlay
  if (!nowPlayingOverlay_) {
    nowPlayingOverlay_ = new QWidget(this);
    nowPlayingOverlay_->setStyleSheet(
        QStringLiteral("background-color: rgba(0, 0, 0, 0.72);"));
    nowPlayingOverlay_->installEventFilter(this);
  }

  // Populate the overlay with game info
  auto *existingLay = nowPlayingOverlay_->layout();
  if (existingLay) {
    QLayoutItem *item;
    while ((item = existingLay->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
  } else {
    auto *newLay = new QVBoxLayout(nowPlayingOverlay_);
    newLay->setAlignment(Qt::AlignCenter);
    newLay->setSpacing(12);
  }

  auto *lay = qobject_cast<QVBoxLayout *>(nowPlayingOverlay_->layout());
  if (lay) {
    auto *heading =
        new QLabel(QStringLiteral("Playing via Steam"), nowPlayingOverlay_);
    QFont headFont;
    headFont.setPixelSize(32);
    headFont.setWeight(QFont::Bold);
    heading->setFont(headFont);
    heading->setAlignment(Qt::AlignCenter);
    heading->setStyleSheet(QStringLiteral("color: #f0f0f0;"));

    auto *appName = new QLabel(steamAppId, nowPlayingOverlay_);
    appName->setAlignment(Qt::AlignCenter);
    appName->setStyleSheet(QStringLiteral("color: #f0f0f0; font-size: 16px;"));

    auto *hint =
        new QLabel(QStringLiteral("Press Esc to dismiss"), nowPlayingOverlay_);
    QFont hintFont;
    hintFont.setPixelSize(13);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.5);"));
    hint->setFont(hintFont);

    lay->addStretch();
    lay->addWidget(heading);
    lay->addWidget(appName);
    lay->addWidget(hint);
    lay->addStretch();
  }

  nowPlayingOverlay_->setGeometry(this->rect());
  nowPlayingOverlay_->raise();
  nowPlayingOverlay_->show();

  // Auto-dismiss in 5 seconds
  QTimer::singleShot(5000, nowPlayingOverlay_, &QWidget::hide);
}

void MainWindow::openStreaming() { goToMainMenu(); }

void MainWindow::setupStreamingPages() {
  auto *yt = new YouTubeBrowsePage(this);
  youTubeBrowsePage = yt;

  auto *ytPlayer = new YouTubePlayerPage(this);
  youTubePlayerPage = ytPlayer;

  auto *web = new StreamingWebViewPage(this);
  streamingWebPage = web;

  connect(web, &StreamingWebViewPage::homeRequested, this,
          &MainWindow::goToMainMenu);

  connect(yt, &YouTubeBrowsePage::homeRequested, this,
          &MainWindow::goToMainMenu);

  connect(yt, &YouTubeBrowsePage::videoRequested, this,
          [this](const QString &url) {
            auto *player = qobject_cast<YouTubePlayerPage *>(youTubePlayerPage);
            if (!player)
              return;
            stackedWidget->setCurrentWidget(youTubePlayerPage);
            player->playVideoUrl(url);
            youTubePlayerPage->setFocus();
          });

  connect(ytPlayer, &YouTubePlayerPage::homeRequested, this,
          &MainWindow::goToMainMenu);
  connect(ytPlayer, &YouTubePlayerPage::backRequested, this, [this]() {
    stackedWidget->setCurrentWidget(youTubeBrowsePage);
    youTubeBrowsePage->setFocus();
  });
}

void MainWindow::launchStreamingApp(int app) {
  const auto selectedApp = static_cast<AIO::GUI::StreamingApp>(app);

  // YouTube uses the Data API + native Qt UI (no WebEngine).
  if (selectedApp == AIO::GUI::StreamingApp::YouTube) {
    if (!youTubeBrowsePage)
      return;
    stackedWidget->setCurrentWidget(youTubeBrowsePage);
    youTubeBrowsePage->setFocus();
    return;
  }

  auto *web = qobject_cast<StreamingWebViewPage *>(streamingWebPage);
  if (!web)
    return;
  stackedWidget->setCurrentWidget(streamingWebPage);
  web->openApp(selectedApp);
  streamingWebPage->setFocus();
}

void MainWindow::setupSettingsPage() {
  settingsPage = new QWidget(this);
  settingsPage->setObjectName("aioSettingsPage");
  QVBoxLayout *layout = new QVBoxLayout(settingsPage);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(18);

  QLabel *title = new QLabel("System Settings", settingsPage);
  title->setAlignment(Qt::AlignCenter);
  title->setProperty("role", "title");
  layout->addWidget(title);

  QLabel *subtitle =
      new QLabel("Library paths and platform behavior for the whole system.",
                 settingsPage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setProperty("role", "subtitle");
  layout->addWidget(subtitle);

  // ROM Directory Setting
  QGroupBox *romGroup = new QGroupBox("ROM Library Path", settingsPage);
  QVBoxLayout *romLayout = new QVBoxLayout(romGroup);

  romPathLabel = new QLabel(romDirectory, romGroup);
  romPathLabel->setWordWrap(true);
  romPathLabel->setObjectName("aioPathLabel");
  romLayout->addWidget(romPathLabel);

  QPushButton *browseBtn = new QPushButton("Browse Folder\u2026", romGroup);
  browseBtn->setCursor(Qt::PointingHandCursor);
  browseBtn->setFocusPolicy(Qt::StrongFocus);
  connect(browseBtn, &QPushButton::clicked, this,
          &MainWindow::selectRomDirectory);
  romLayout->addWidget(browseBtn);

  layout->addWidget(romGroup);

  layout->addStretch();

  QPushButton *advancedRomPickerBtn =
      new QPushButton("Advanced: ROM Picker", settingsPage);
  advancedRomPickerBtn->setCursor(Qt::PointingHandCursor);
  advancedRomPickerBtn->setFocusPolicy(Qt::StrongFocus);
  advancedRomPickerBtn->setProperty("variant", "secondary");
  connect(advancedRomPickerBtn, &QPushButton::clicked, this,
          &MainWindow::goToEmulatorSelect);
  layout->addWidget(advancedRomPickerBtn);

  QPushButton *backBtn = new QPushButton("\u2190  Back", settingsPage);
  backBtn->setCursor(Qt::PointingHandCursor);
  backBtn->setFocusPolicy(Qt::StrongFocus);
  backBtn->setProperty("variant", "secondary");
  connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);
  layout->addWidget(backBtn);

  // Create adapter for settings menu
  settingsMenuAdapter = std::make_unique<SettingsMenuAdapter>(
      settingsPage,
      std::vector<QPushButton *>{browseBtn, advancedRomPickerBtn, backBtn},
      this);
}

void MainWindow::setupEmulatorSettingsPage() {
  emulatorSettingsPage = new QWidget(this);
  emulatorSettingsPage->setObjectName("aioEmulatorSettingsPage");
  QVBoxLayout *pageLayout = new QVBoxLayout(emulatorSettingsPage);
  pageLayout->setContentsMargins(0, 0, 0, 0);
  pageLayout->setSpacing(0);

  // Scroll container so this page never forces the window off-screen.
  auto *scroll = new QScrollArea(emulatorSettingsPage);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  auto *content = new QWidget(scroll);
  scroll->setWidget(content);

  QVBoxLayout *layout = new QVBoxLayout(content);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(16);

  pageLayout->addWidget(scroll);

  QLabel *title = new QLabel("Emulator Settings", content);
  title->setAlignment(Qt::AlignCenter);
  title->setProperty("role", "title");
  layout->addWidget(title);

  QLabel *subtitle = new QLabel(
      "Video scaling, controller tuning, and runtime behavior.", content);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setProperty("role", "subtitle");
  layout->addWidget(subtitle);

  std::vector<QPushButton *> navButtons;

  // Graphics (scaling)
  {
    QGroupBox *grp = new QGroupBox("Graphics", content);
    QVBoxLayout *l = new QVBoxLayout(grp);

    auto *scaleLabel = new QLabel(grp);
    auto refreshScaleLabel = [this, scaleLabel]() {
      const QString mode = (videoScaleMode_ == VideoScaleMode::IntegerNearest)
                               ? QStringLiteral("Pixel Perfect (Integer)")
                               : QStringLiteral("Fit Window (Nearest)");
      const QString scale = (videoIntegerScale_ > 0)
                                ? QString::number(videoIntegerScale_)
                                : QStringLiteral("Auto");
      scaleLabel->setText(
          QStringLiteral("Scaling: %1 | Integer scale: %2").arg(mode, scale));
    };
    refreshScaleLabel();
    l->addWidget(scaleLabel);

    QHBoxLayout *row = new QHBoxLayout();
    QPushButton *pixelPerfectBtn = new QPushButton("Pixel Perfect", grp);
    QPushButton *fitBtn = new QPushButton("Fit Window", grp);
    QPushButton *scaleMinus = new QPushButton("Scale \u2212", grp);
    QPushButton *scalePlus = new QPushButton("Scale +", grp);
    for (auto *b : {pixelPerfectBtn, fitBtn, scaleMinus, scalePlus}) {
      b->setCursor(Qt::PointingHandCursor);
      b->setFocusPolicy(Qt::StrongFocus);
      b->setProperty("variant", "secondary");
      row->addWidget(b);
      navButtons.push_back(b);
    }
    l->addLayout(row);

    connect(pixelPerfectBtn, &QPushButton::clicked, this,
            [this, refreshScaleLabel]() {
              videoScaleMode_ = VideoScaleMode::IntegerNearest;
              settings.setValue("video/gba/scaleMode",
                                static_cast<int>(videoScaleMode_));
              refreshScaleLabel();
            });
    connect(fitBtn, &QPushButton::clicked, this, [this, refreshScaleLabel]() {
      videoScaleMode_ = VideoScaleMode::FitNearest;
      settings.setValue("video/gba/scaleMode",
                        static_cast<int>(videoScaleMode_));
      refreshScaleLabel();
    });
    connect(scaleMinus, &QPushButton::clicked, this,
            [this, refreshScaleLabel]() {
              if (videoIntegerScale_ <= 0) {
                videoIntegerScale_ = 1;
              } else {
                videoIntegerScale_ = std::max(1, videoIntegerScale_ - 1);
              }
              settings.setValue("video/gba/integerScale", videoIntegerScale_);
              refreshScaleLabel();
            });
    connect(scalePlus, &QPushButton::clicked, this,
            [this, refreshScaleLabel]() {
              if (videoIntegerScale_ <= 0) {
                videoIntegerScale_ = 2;
              } else {
                videoIntegerScale_ = std::min(12, videoIntegerScale_ + 1);
              }
              settings.setValue("video/gba/integerScale", videoIntegerScale_);
              refreshScaleLabel();
            });

    layout->addWidget(grp);
  }

  // Controls
  {
    QGroupBox *grp = new QGroupBox("Controls", content);
    QVBoxLayout *l = new QVBoxLayout(grp);

    emuSettingsStatusLabel_ = new QLabel("Select an action to rebind.", grp);
    emuSettingsStatusLabel_->setWordWrap(true);
    l->addWidget(emuSettingsStatusLabel_);

    auto *deadzoneLabel = new QLabel(grp);
    auto refreshDeadzoneLabel = [deadzoneLabel]() {
      const auto &im = AIO::Input::InputManager::instance();
      deadzoneLabel->setText(
          QStringLiteral("Stick deadzone: press=%1 release=%2")
              .arg(im.stickPressDeadzone())
              .arg(im.stickReleaseDeadzone()));
    };
    refreshDeadzoneLabel();
    l->addWidget(deadzoneLabel);

    QHBoxLayout *dzRow = new QHBoxLayout();
    QPushButton *pressMinus = new QPushButton("Press \u2212", grp);
    QPushButton *pressPlus = new QPushButton("Press +", grp);
    QPushButton *releaseMinus = new QPushButton("Release \u2212", grp);
    QPushButton *releasePlus = new QPushButton("Release +", grp);
    for (auto *b : {pressMinus, pressPlus, releaseMinus, releasePlus}) {
      b->setCursor(Qt::PointingHandCursor);
      b->setFocusPolicy(Qt::StrongFocus);
      b->setProperty("variant", "secondary");
      dzRow->addWidget(b);
      navButtons.push_back(b);
    }
    l->addLayout(dzRow);

    auto adjustDeadzone = [&](int dp, int dr) {
      auto &im = AIO::Input::InputManager::instance();
      const int press = std::max(0, im.stickPressDeadzone() + dp);
      const int release = std::max(0, im.stickReleaseDeadzone() + dr);
      im.setStickDeadzones(press, release);
      refreshDeadzoneLabel();
    };
    connect(pressMinus, &QPushButton::clicked, this,
            [=]() { adjustDeadzone(-500, 0); });
    connect(pressPlus, &QPushButton::clicked, this,
            [=]() { adjustDeadzone(+500, 0); });
    connect(releaseMinus, &QPushButton::clicked, this,
            [=]() { adjustDeadzone(0, -500); });
    connect(releasePlus, &QPushButton::clicked, this,
            [=]() { adjustDeadzone(0, +500); });

    auto addRebind = [&](const QString &name,
                         AIO::Input::LogicalButton logical) {
      auto *btn = new QPushButton(QStringLiteral("Rebind %1").arg(name), grp);
      btn->setCursor(Qt::PointingHandCursor);
      btn->setFocusPolicy(Qt::StrongFocus);
      connect(btn, &QPushButton::clicked, this, [this, logical]() {
        emuSettingsCapturingRebind_ = true;
        emuSettingsCaptureLogical_ = logical;
        (void)AIO::Input::InputManager::instance()
            .consumeLastControllerButtonDown();
        if (emuSettingsStatusLabel_) {
          emuSettingsStatusLabel_->setText(QStringLiteral(
              "Press a key or controller button to bind… (Esc cancels)"));
        }
      });
      l->addWidget(btn);
      navButtons.push_back(btn);
    };

    addRebind("A", AIO::Input::LogicalButton::Confirm);
    addRebind("B", AIO::Input::LogicalButton::Back);
    addRebind("START", AIO::Input::LogicalButton::Start);
    addRebind("SELECT", AIO::Input::LogicalButton::Select);
    addRebind("L", AIO::Input::LogicalButton::L);
    addRebind("R", AIO::Input::LogicalButton::R);
    addRebind("UP", AIO::Input::LogicalButton::Up);
    addRebind("DOWN", AIO::Input::LogicalButton::Down);
    addRebind("LEFT", AIO::Input::LogicalButton::Left);
    addRebind("RIGHT", AIO::Input::LogicalButton::Right);

    layout->addWidget(grp);
  }

  // Sound
  {
    QGroupBox *grp = new QGroupBox("Sound", content);
    QVBoxLayout *l = new QVBoxLayout(grp);

    QLabel *volLabel = new QLabel("Master Volume", grp);
    l->addWidget(volLabel);

    QSlider *volSlider = new QSlider(Qt::Horizontal, grp);
    volSlider->setObjectName("aioVolumeSlider");
    volSlider->setRange(0, 100);
    volSlider->setValue(80);
    l->addWidget(volSlider);

    QCheckBox *muteBox = new QCheckBox("Mute audio", grp);
    l->addWidget(muteBox);

    layout->addWidget(grp);
  }

  layout->addStretch();

  QPushButton *resumeBtn = new QPushButton("\u25B6  Resume", content);
  resumeBtn->setCursor(Qt::PointingHandCursor);
  resumeBtn->setFocusPolicy(Qt::StrongFocus);
  resumeBtn->setProperty("variant", "secondary");
  connect(resumeBtn, &QPushButton::clicked, this,
          &MainWindow::closeEmulatorSettings);
  layout->addWidget(resumeBtn);
  navButtons.push_back(resumeBtn);

  emulatorSettingsAdapter = std::make_unique<AIO::GUI::EmulatorSettingsAdapter>(
      emulatorSettingsPage, navButtons, this);
}

void MainWindow::setupEmulatorSelect() {
  emulatorSelectPage = new QWidget(this);
  emulatorSelectPage->setObjectName("aioEmulatorSelectPage");
  QVBoxLayout *layout = new QVBoxLayout(emulatorSelectPage);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(20);

  QLabel *title = new QLabel("Select System", emulatorSelectPage);
  title->setAlignment(Qt::AlignCenter);
  title->setProperty("role", "title");
  layout->addWidget(title);

  QLabel *subtitle = new QLabel(
      "Choose a platform and continue into its library.", emulatorSelectPage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setProperty("role", "subtitle");
  layout->addWidget(subtitle);

  QPushButton *gbaBtn = new QPushButton("Game Boy Advance", emulatorSelectPage);
  gbaBtn->setCursor(Qt::PointingHandCursor);
  gbaBtn->setFocusPolicy(Qt::StrongFocus);
  gbaBtn->setProperty("system", "gba");
  connect(gbaBtn, &QPushButton::clicked, this, [this]() {
    currentEmulator = EmulatorType::GBA;
    goToGameSelect();
  });
  layout->addWidget(gbaBtn);

  QPushButton *ps1Btn = new QPushButton("PlayStation", emulatorSelectPage);
  ps1Btn->setCursor(Qt::PointingHandCursor);
  ps1Btn->setFocusPolicy(Qt::StrongFocus);
  ps1Btn->setProperty("system", "ps1");
  connect(ps1Btn, &QPushButton::clicked, this, [this]() {
    currentEmulator = EmulatorType::PS1;
    goToGameSelect();
  });
  layout->addWidget(ps1Btn);

  QPushButton *switchBtn =
      new QPushButton("Nintendo Switch", emulatorSelectPage);
  switchBtn->setCursor(Qt::PointingHandCursor);
  switchBtn->setFocusPolicy(Qt::StrongFocus);
  switchBtn->setProperty("system", "switch");
  connect(switchBtn, &QPushButton::clicked, this, [this]() {
    currentEmulator = EmulatorType::Switch;
    goToGameSelect();
  });
  layout->addWidget(switchBtn);

  layout->addStretch();

  QPushButton *backBtn = new QPushButton("\u2190  Back", emulatorSelectPage);
  backBtn->setCursor(Qt::PointingHandCursor);
  backBtn->setFocusPolicy(Qt::StrongFocus);
  backBtn->setProperty("variant", "secondary");
  connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);
  layout->addWidget(backBtn);

  // Create adapter for emulator selection with all buttons including back
  emulatorSelectAdapter = std::make_unique<EmulatorSelectAdapter>(
      emulatorSelectPage,
      std::vector<QPushButton *>{gbaBtn, ps1Btn, switchBtn, backBtn}, this);
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
  } else if (currentEmulator == EmulatorType::PS1) {
    filters << "*.bin" << "*.cue" << "*.iso" << "*.img";
  } else if (currentEmulator == EmulatorType::Switch) {
    filters << "*.nso" << "*.nro" << "*.xci" << "*.nsp";
  }

  QDirIterator it(romDirectory, filters, QDir::Files,
                  QDirIterator::Subdirectories);
  bool foundAny = false;

  while (it.hasNext()) {
    it.next();
    QFileInfo fileInfo = it.fileInfo();
    foundAny = true;

    QListWidgetItem *item = new QListWidgetItem();
    item->setText(fileInfo.completeBaseName());
    item->setData(Qt::UserRole, fileInfo.absoluteFilePath());

    // Generate themed game tile icon
    QPixmap pixmap(180, 120);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // System-specific brand colors
    QColor brandPrimary, brandDark;
    QString sysName;
    if (currentEmulator == EmulatorType::GBA) {
      brandPrimary = QColor(124, 140, 255);
      brandDark = QColor(44, 52, 120);
      sysName = "GBA";
    } else if (currentEmulator == EmulatorType::PS1) {
      brandPrimary = QColor(77, 196, 255);
      brandDark = QColor(20, 68, 110);
      sysName = "PS1";
    } else {
      brandPrimary = QColor(255, 118, 112);
      brandDark = QColor(120, 38, 35);
      sysName = "NSW";
    }

    // Rounded rect background with gradient
    QPainterPath tilePath;
    tilePath.addRoundedRect(QRectF(0, 0, 180, 120), 12, 12);
    QLinearGradient bg(0, 0, 0, 120);
    bg.setColorAt(0.0, brandDark);
    bg.setColorAt(1.0, brandDark.darker(180));
    painter.fillPath(tilePath, bg);

    // Subtle radial highlight
    QRadialGradient rg(90, 50, 100);
    rg.setColorAt(0.0, QColor(255, 255, 255, 10));
    rg.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillPath(tilePath, rg);

    // System badge — small colored pill at top
    QFont font = painter.font();
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    painter.setFont(font);
    QRectF badgeRect(10, 8, 42, 18);
    QPainterPath badgePath;
    badgePath.addRoundedRect(badgeRect, 6, 6);
    painter.fillPath(badgePath, QColor(brandPrimary.red(), brandPrimary.green(),
                                       brandPrimary.blue(), 50));
    painter.setPen(brandPrimary);
    painter.drawText(badgeRect, Qt::AlignCenter, sysName);

    // Game initial — large centered character
    font.setPixelSize(44);
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    QString initial = fileInfo.completeBaseName().left(1).toUpper();
    // Shadow
    painter.setPen(QColor(0, 0, 0, 50));
    painter.drawText(QRect(0, 18, 180, 90), Qt::AlignCenter, initial);
    // Main text
    painter.setPen(QColor(255, 255, 255, 210));
    painter.drawText(QRect(0, 16, 180, 90), Qt::AlignCenter, initial);

    // Bottom edge: truncated game name
    font.setPixelSize(10);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    QFontMetrics fm(font);
    QString displayName =
        fm.elidedText(fileInfo.completeBaseName(), Qt::ElideRight, 160);
    painter.setPen(QColor(255, 255, 255, 100));
    painter.drawText(QRect(10, 98, 160, 16), Qt::AlignLeft | Qt::AlignVCenter,
                     displayName);

    painter.end();

    item->setIcon(QIcon(pixmap));
    gameListWidget->addItem(item);
  }

  if (!foundAny) {
    gameListWidget->addItem("No ROMs found in " + romDirectory);
  }
}

void MainWindow::startGame(const QString &path) {
  LoadROM(path.toStdString());
  stackedWidget->setCurrentWidget(emulatorPage);
  setFocus(); // Ensure window has focus for input
}

void MainWindow::stopGame() {
  StopEmulatorThread();
  displayTimer->stop();
  goToGamesLibrary();
}

void MainWindow::stopGameToHome() {
  StopEmulatorThread();
  displayTimer->stop();
  goToMainMenu();
}

void MainWindow::goToMainMenu() {
  if (homeScreenPage) {
    stackedWidget->setCurrentWidget(homeScreenPage);
    homeScreenPage->setFocus();
  } else {
    stackedWidget->setCurrentWidget(mainMenuPage);
    if (mainMenuPage) {
      if (auto *btn = mainMenuPage->findChild<QPushButton *>())
        btn->setFocus();
      else
        mainMenuPage->setFocus();
    }
  }
}

void MainWindow::goToSettings() {
  stackedWidget->setCurrentWidget(settingsPage);
  if (settingsPage) {
    if (auto *btn = settingsPage->findChild<QPushButton *>())
      btn->setFocus();
    else
      settingsPage->setFocus();
  }
}

void MainWindow::goToEmulatorSettings() {
  if (!emulatorSettingsPage)
    return;
  stackedWidget->setCurrentWidget(emulatorSettingsPage);
  if (emulatorSettingsPage) {
    if (auto *btn = emulatorSettingsPage->findChild<QPushButton *>())
      btn->setFocus();
    else
      emulatorSettingsPage->setFocus();
  }
}

void MainWindow::closeEmulatorSettings() {
  emuSettingsCapturingRebind_ = false;
  if (!emulatorPage)
    return;
  stackedWidget->setCurrentWidget(emulatorPage);
  setFocus();
}

void MainWindow::goToEmulatorSelect() {
  stackedWidget->setCurrentWidget(emulatorSelectPage);
  if (emulatorSelectPage) {
    if (auto *btn = emulatorSelectPage->findChild<QPushButton *>())
      btn->setFocus();
    else
      emulatorSelectPage->setFocus();
  }
}

void MainWindow::goToGameSelect() {
  refreshGameList();
  stackedWidget->setCurrentWidget(gameSelectPage);
  if (gameListWidget) {
    if (gameListWidget->count() > 0)
      gameListWidget->setCurrentRow(0);
    gameListWidget->setFocus();
  } else if (gameSelectPage) {
    gameSelectPage->setFocus();
  }
}

void MainWindow::setupGameSelect() {
  gameSelectPage = new QWidget(this);
  gameSelectPage->setObjectName("aioGameSelectPage");
  QVBoxLayout *layout = new QVBoxLayout(gameSelectPage);
  layout->setContentsMargins(50, 50, 50, 50);
  layout->setSpacing(18);

  QLabel *title = new QLabel("Select Game", gameSelectPage);
  title->setAlignment(Qt::AlignCenter);
  title->setProperty("role", "title");
  layout->addWidget(title);

  QLabel *subtitle = new QLabel(
      "Browse your library and launch directly into play.", gameSelectPage);
  subtitle->setAlignment(Qt::AlignCenter);
  subtitle->setProperty("role", "subtitle");
  layout->addWidget(subtitle);

  gameListWidget = new QListWidget(gameSelectPage);
  gameListWidget->setObjectName("aioGameGrid");
  gameListWidget->setFocusPolicy(Qt::StrongFocus);
  gameListWidget->setIconSize(QSize(180, 120));
  gameListWidget->setViewMode(QListWidget::IconMode);
  gameListWidget->setResizeMode(QListWidget::Adjust);
  gameListWidget->setSpacing(15);
  gameListWidget->setMovement(QListWidget::Static);

  connect(gameListWidget, &QListWidget::itemActivated, this,
          [this](QListWidgetItem *item) {
            QString fullPath = item->data(Qt::UserRole).toString();
            startGame(fullPath);
          });

  layout->addWidget(gameListWidget);

  QPushButton *backBtn = new QPushButton("\u2190  Back", gameSelectPage);
  backBtn->setCursor(Qt::PointingHandCursor);
  backBtn->setFocusPolicy(Qt::StrongFocus);
  backBtn->setProperty("variant", "secondary");
  connect(backBtn, &QPushButton::clicked, this,
          &MainWindow::goToEmulatorSelect);
  layout->addWidget(backBtn);

  // Create adapter for game select; navigation operates on the ROM list,
  // while back is handled by the adapter's back() override.
  gameSelectAdapter = std::make_unique<GameSelectAdapter>(
      gameSelectPage, std::vector<QPushButton *>{backBtn}, this,
      gameListWidget);
}

void MainWindow::setupEmulatorView() {
  emulatorPage = new QWidget(this);
  emulatorPage->setObjectName("aioEmulatorPage");
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

  const int mode = settings
                       .value("video/gba/scaleMode",
                              static_cast<int>(VideoScaleMode::IntegerNearest))
                       .toInt();
  if (mode == static_cast<int>(VideoScaleMode::FitNearest)) {
    videoScaleMode_ = VideoScaleMode::FitNearest;
  } else {
    videoScaleMode_ = VideoScaleMode::IntegerNearest;
  }
  videoIntegerScale_ = settings.value("video/gba/integerScale", 0).toInt();
}

void MainWindow::selectRomDirectory() {
  QString dir = QFileDialog::getExistingDirectory(this, "Select ROM Directory",
                                                  romDirectory);
  if (!dir.isEmpty()) {
    romDirectory = dir;
    settings.setValue("romDirectory", romDirectory);
    romPathLabel->setText(romDirectory);
  }
}

} // namespace GUI
} // namespace AIO
