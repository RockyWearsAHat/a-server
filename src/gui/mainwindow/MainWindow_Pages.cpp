#include "gui/MainWindow.h"

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
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "gui/ActionBindingsDialog.h"
#include "gui/EmulatorSelectAdapter.h"
#include "gui/GameSelectAdapter.h"
#include "gui/MainMenuAdapter.h"
#include "gui/NASAdapter.h"
#include "gui/NASPage.h"
#include "gui/SettingsMenuAdapter.h"
#include "gui/StreamingHubWidget.h"
#include "gui/StreamingWebViewPage.h"
#include "gui/YouTubeBrowsePage.h"
#include "gui/YouTubePlayerPage.h"

namespace AIO {
namespace GUI {

void MainWindow::setupMainMenu() {
    mainMenuPage = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(mainMenuPage);
    layout->setContentsMargins(50, 50, 50, 50);
    layout->setSpacing(20);

    auto makeMenuButton = [&](const QString& text, auto slot) -> QPushButton* {
        auto* b = new QPushButton(text, mainMenuPage);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::StrongFocus);
        connect(b, &QPushButton::clicked, this, slot);
        layout->addWidget(b);
        return b;
    };

    QLabel *title = new QLabel("AIO ENTERTAINMENT SYSTEM", mainMenuPage);
    title->setAlignment(Qt::AlignCenter);
    title->setProperty("role", "title");
    layout->addWidget(title);

    QPushButton* emuBtn = makeMenuButton("EMULATORS", &MainWindow::goToEmulatorSelect);
    QPushButton* streamBtn = makeMenuButton("STREAMING", &MainWindow::openStreaming);
    QPushButton* nasBtn = makeMenuButton("NAS", &MainWindow::goToNAS);
    QPushButton* settingsBtn = makeMenuButton("SETTINGS", &MainWindow::goToSettings);

    layout->addStretch();

    QLabel *footer = new QLabel("v1.0.0 | System Ready", mainMenuPage);
    footer->setAlignment(Qt::AlignCenter);
    footer->setProperty("role", "subtitle");
    layout->addWidget(footer);

    // State-driven navigation adapter (single unified outline).
    mainMenuAdapter = std::make_unique<AIO::GUI::MainMenuAdapter>(this, mainMenuPage,
        std::vector<QPushButton*>{emuBtn, streamBtn, nasBtn, settingsBtn});
}

void MainWindow::goToNAS() {
    if (!nasPage) return;
    stackedWidget->setCurrentWidget(nasPage);
    nasPage->setFocus();
}

void MainWindow::setupNASPage() {
    auto* page = new NASPage(this);
    nasPage = page;
    connect(page, &NASPage::homeRequested, this, &MainWindow::goToMainMenu);

    nasAdapter = std::make_unique<AIO::GUI::NASAdapter>(
        nasPage,
        std::vector<QPushButton*>{
            page->upButton(),
            page->refreshButton(),
            page->mkdirButton(),
            page->renameButton(),
            page->deleteButton(),
            page->uploadButton(),
            page->backButton(),
        },
        page->listWidget()
    );
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

    // Input bindings
    QGroupBox* inputGroup = new QGroupBox("Controls", settingsPage);
    QVBoxLayout* inputLayout = new QVBoxLayout(inputGroup);

    QPushButton* navControlsBtn = new QPushButton("SYSTEM NAVIGATION CONTROLS...", inputGroup);
    navControlsBtn->setCursor(Qt::PointingHandCursor);
    navControlsBtn->setFocusPolicy(Qt::StrongFocus);
    connect(navControlsBtn, &QPushButton::clicked, this, [this]() {
        AIO::GUI::ActionBindingsDialog dlg(AIO::Input::AppId::System, this);
        dlg.exec();
    });
    inputLayout->addWidget(navControlsBtn);

    QPushButton* gbaControlsBtn = new QPushButton("GBA CONTROLS...", inputGroup);
    gbaControlsBtn->setCursor(Qt::PointingHandCursor);
    gbaControlsBtn->setFocusPolicy(Qt::StrongFocus);
    connect(gbaControlsBtn, &QPushButton::clicked, this, [this]() {
        AIO::GUI::ActionBindingsDialog dlg(AIO::Input::AppId::GBA, this);
        dlg.exec();
    });
    inputLayout->addWidget(gbaControlsBtn);

    QPushButton* ytControlsBtn = new QPushButton("YOUTUBE CONTROLS...", inputGroup);
    ytControlsBtn->setCursor(Qt::PointingHandCursor);
    ytControlsBtn->setFocusPolicy(Qt::StrongFocus);
    connect(ytControlsBtn, &QPushButton::clicked, this, [this]() {
        AIO::GUI::ActionBindingsDialog dlg(AIO::Input::AppId::YouTube, this);
        dlg.exec();
    });
    inputLayout->addWidget(ytControlsBtn);

    layout->addWidget(inputGroup);

    layout->addStretch();

    QPushButton *backBtn = new QPushButton("BACK TO MENU", settingsPage);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setFocusPolicy(Qt::StrongFocus);
    backBtn->setProperty("variant", "secondary");
    connect(backBtn, &QPushButton::clicked, this, &MainWindow::goToMainMenu);
    layout->addWidget(backBtn);

    // Create adapter for settings menu
    settingsMenuAdapter = std::make_unique<SettingsMenuAdapter>(
        settingsPage,
        std::vector<QPushButton*>{browseBtn, navControlsBtn, gbaControlsBtn, ytControlsBtn, backBtn},
        this
    );
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

    // Create adapter for emulator selection with all buttons including back
    emulatorSelectAdapter = std::make_unique<EmulatorSelectAdapter>(
        emulatorSelectPage,
        std::vector<QPushButton*>{gbaBtn, switchBtn, backBtn},
        this
    );
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
    if (mainMenuPage) {
        if (auto* btn = mainMenuPage->findChild<QPushButton*>()) btn->setFocus();
        else mainMenuPage->setFocus();
    }
}

void MainWindow::goToSettings() {
    stackedWidget->setCurrentWidget(settingsPage);
    if (settingsPage) {
        if (auto* btn = settingsPage->findChild<QPushButton*>()) btn->setFocus();
        else settingsPage->setFocus();
    }
}

void MainWindow::goToEmulatorSelect() {
    stackedWidget->setCurrentWidget(emulatorSelectPage);
    if (emulatorSelectPage) {
        if (auto* btn = emulatorSelectPage->findChild<QPushButton*>()) btn->setFocus();
        else emulatorSelectPage->setFocus();
    }
}

void MainWindow::goToGameSelect() {
    refreshGameList();
    stackedWidget->setCurrentWidget(gameSelectPage);
    if (gameListWidget) {
        if (gameListWidget->count() > 0) gameListWidget->setCurrentRow(0);
        gameListWidget->setFocus();
    } else if (gameSelectPage) {
        gameSelectPage->setFocus();
    }
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

    // Create adapter for game select; navigation operates on the ROM list,
    // while back is handled by the adapter's back() override.
    gameSelectAdapter = std::make_unique<GameSelectAdapter>(
        gameSelectPage,
        std::vector<QPushButton*>{backBtn},
        this,
        gameListWidget
    );
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
