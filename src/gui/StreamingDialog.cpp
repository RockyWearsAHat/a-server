#include "gui/StreamingDialog.h"
#include <QMessageBox>
#include <QGroupBox>

namespace AIO {
namespace GUI {

StreamingDialog::StreamingDialog(QWidget* parent)
    : QDialog(parent),
      streamingManager_(Streaming::StreamingManager::getInstance())
{
    setWindowTitle("Streaming Services");
    resize(900, 700);
    setupUI();
}

void StreamingDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    tabWidget_ = new QTabWidget(this);
    
    setupYouTubeTab();
    setupNetflixTab();
    setupDisneyPlusTab();
    setupHuluTab();
    
    mainLayout->addWidget(tabWidget_);
    
    connect(tabWidget_, &QTabWidget::currentChanged, this, &StreamingDialog::onServiceTabChanged);
}

void StreamingDialog::setupYouTubeTab() {
    youtubeTab_ = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(youtubeTab_);
    
    // Login section
    QGroupBox* loginGroup = new QGroupBox("Authentication");
    QVBoxLayout* loginLayout = new QVBoxLayout(loginGroup);
    
    ServiceTab& tab = serviceTabs_[0];
    tab.serviceType = Streaming::StreamingServiceType::YouTube;
    
    tab.apiKeyEdit = new QLineEdit();
    tab.apiKeyEdit->setPlaceholderText("YouTube API Key (for public content)");
    loginLayout->addWidget(new QLabel("API Key:"));
    loginLayout->addWidget(tab.apiKeyEdit);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    tab.loginButton = new QPushButton("Connect");
    tab.logoutButton = new QPushButton("Disconnect");
    tab.logoutButton->setEnabled(false);
    buttonLayout->addWidget(tab.loginButton);
    buttonLayout->addWidget(tab.logoutButton);
    loginLayout->addLayout(buttonLayout);
    
    tab.statusLabel = new QLabel("Status: Not connected");
    loginLayout->addWidget(tab.statusLabel);
    
    layout->addWidget(loginGroup);
    
    // Search section
    QGroupBox* searchGroup = new QGroupBox("Search");
    QVBoxLayout* searchLayout = new QVBoxLayout(searchGroup);
    
    QHBoxLayout* searchInputLayout = new QHBoxLayout();
    tab.searchEdit = new QLineEdit();
    tab.searchEdit->setPlaceholderText("Search YouTube...");
    tab.searchButton = new QPushButton("Search");
    searchInputLayout->addWidget(tab.searchEdit);
    searchInputLayout->addWidget(tab.searchButton);
    searchLayout->addLayout(searchInputLayout);
    
    layout->addWidget(searchGroup);
    
    // Content list
    QGroupBox* contentGroup = new QGroupBox("Videos");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentGroup);
    
    tab.contentList = new QListWidget();
    contentLayout->addWidget(tab.contentList);
    
    tab.playButton = new QPushButton("Play Selected");
    tab.playButton->setEnabled(false);
    contentLayout->addWidget(tab.playButton);
    
    layout->addWidget(contentGroup);
    
    tabWidget_->addTab(youtubeTab_, "YouTube");
    
    // Connect signals
    connect(tab.loginButton, &QPushButton::clicked, this, &StreamingDialog::onLoginClicked);
    connect(tab.logoutButton, &QPushButton::clicked, this, &StreamingDialog::onLogoutClicked);
    connect(tab.searchButton, &QPushButton::clicked, this, &StreamingDialog::onSearchClicked);
    connect(tab.playButton, &QPushButton::clicked, this, &StreamingDialog::onPlayClicked);
    connect(tab.contentList, &QListWidget::itemClicked, this, &StreamingDialog::onContentSelected);
}

void StreamingDialog::setupNetflixTab() {
    netflixTab_ = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(netflixTab_);
    
    QGroupBox* loginGroup = new QGroupBox("Authentication");
    QVBoxLayout* loginLayout = new QVBoxLayout(loginGroup);
    
    ServiceTab& tab = serviceTabs_[1];
    tab.serviceType = Streaming::StreamingServiceType::Netflix;
    
    tab.usernameEdit = new QLineEdit();
    tab.usernameEdit->setPlaceholderText("Email or phone number");
    tab.passwordEdit = new QLineEdit();
    tab.passwordEdit->setEchoMode(QLineEdit::Password);
    tab.passwordEdit->setPlaceholderText("Password");
    
    loginLayout->addWidget(new QLabel("Email/Phone:"));
    loginLayout->addWidget(tab.usernameEdit);
    loginLayout->addWidget(new QLabel("Password:"));
    loginLayout->addWidget(tab.passwordEdit);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    tab.loginButton = new QPushButton("Login");
    tab.logoutButton = new QPushButton("Logout");
    tab.logoutButton->setEnabled(false);
    buttonLayout->addWidget(tab.loginButton);
    buttonLayout->addWidget(tab.logoutButton);
    loginLayout->addLayout(buttonLayout);
    
    tab.statusLabel = new QLabel("Status: Not logged in");
    loginLayout->addWidget(tab.statusLabel);
    
    layout->addWidget(loginGroup);
    
    QGroupBox* searchGroup = new QGroupBox("Search");
    QVBoxLayout* searchLayout = new QVBoxLayout(searchGroup);
    
    QHBoxLayout* searchInputLayout = new QHBoxLayout();
    tab.searchEdit = new QLineEdit();
    tab.searchEdit->setPlaceholderText("Search Netflix...");
    tab.searchButton = new QPushButton("Search");
    searchInputLayout->addWidget(tab.searchEdit);
    searchInputLayout->addWidget(tab.searchButton);
    searchLayout->addLayout(searchInputLayout);
    
    layout->addWidget(searchGroup);
    
    QGroupBox* contentGroup = new QGroupBox("Content");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentGroup);
    
    tab.contentList = new QListWidget();
    contentLayout->addWidget(tab.contentList);
    
    tab.playButton = new QPushButton("Play Selected");
    tab.playButton->setEnabled(false);
    contentLayout->addWidget(tab.playButton);
    
    layout->addWidget(contentGroup);
    
    tabWidget_->addTab(netflixTab_, "Netflix");
    
    connect(tab.loginButton, &QPushButton::clicked, this, &StreamingDialog::onLoginClicked);
    connect(tab.logoutButton, &QPushButton::clicked, this, &StreamingDialog::onLogoutClicked);
    connect(tab.searchButton, &QPushButton::clicked, this, &StreamingDialog::onSearchClicked);
    connect(tab.playButton, &QPushButton::clicked, this, &StreamingDialog::onPlayClicked);
    connect(tab.contentList, &QListWidget::itemClicked, this, &StreamingDialog::onContentSelected);
}

void StreamingDialog::setupDisneyPlusTab() {
    disneyPlusTab_ = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(disneyPlusTab_);
    
    QGroupBox* loginGroup = new QGroupBox("Authentication");
    QVBoxLayout* loginLayout = new QVBoxLayout(loginGroup);
    
    ServiceTab& tab = serviceTabs_[2];
    tab.serviceType = Streaming::StreamingServiceType::DisneyPlus;
    
    tab.usernameEdit = new QLineEdit();
    tab.usernameEdit->setPlaceholderText("Email");
    tab.passwordEdit = new QLineEdit();
    tab.passwordEdit->setEchoMode(QLineEdit::Password);
    tab.passwordEdit->setPlaceholderText("Password");
    
    loginLayout->addWidget(new QLabel("Email:"));
    loginLayout->addWidget(tab.usernameEdit);
    loginLayout->addWidget(new QLabel("Password:"));
    loginLayout->addWidget(tab.passwordEdit);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    tab.loginButton = new QPushButton("Login");
    tab.logoutButton = new QPushButton("Logout");
    tab.logoutButton->setEnabled(false);
    buttonLayout->addWidget(tab.loginButton);
    buttonLayout->addWidget(tab.logoutButton);
    loginLayout->addLayout(buttonLayout);
    
    tab.statusLabel = new QLabel("Status: Not logged in");
    loginLayout->addWidget(tab.statusLabel);
    
    layout->addWidget(loginGroup);
    
    QGroupBox* searchGroup = new QGroupBox("Search");
    QVBoxLayout* searchLayout = new QVBoxLayout(searchGroup);
    
    QHBoxLayout* searchInputLayout = new QHBoxLayout();
    tab.searchEdit = new QLineEdit();
    tab.searchEdit->setPlaceholderText("Search Disney+...");
    tab.searchButton = new QPushButton("Search");
    searchInputLayout->addWidget(tab.searchEdit);
    searchInputLayout->addWidget(tab.searchButton);
    searchLayout->addLayout(searchInputLayout);
    
    layout->addWidget(searchGroup);
    
    QGroupBox* contentGroup = new QGroupBox("Content");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentGroup);
    
    tab.contentList = new QListWidget();
    contentLayout->addWidget(tab.contentList);
    
    tab.playButton = new QPushButton("Play Selected");
    tab.playButton->setEnabled(false);
    contentLayout->addWidget(tab.playButton);
    
    layout->addWidget(contentGroup);
    
    tabWidget_->addTab(disneyPlusTab_, "Disney+");
    
    connect(tab.loginButton, &QPushButton::clicked, this, &StreamingDialog::onLoginClicked);
    connect(tab.logoutButton, &QPushButton::clicked, this, &StreamingDialog::onLogoutClicked);
    connect(tab.searchButton, &QPushButton::clicked, this, &StreamingDialog::onSearchClicked);
    connect(tab.playButton, &QPushButton::clicked, this, &StreamingDialog::onPlayClicked);
    connect(tab.contentList, &QListWidget::itemClicked, this, &StreamingDialog::onContentSelected);
}

void StreamingDialog::setupHuluTab() {
    huluTab_ = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(huluTab_);
    
    QGroupBox* loginGroup = new QGroupBox("Authentication");
    QVBoxLayout* loginLayout = new QVBoxLayout(loginGroup);
    
    ServiceTab& tab = serviceTabs_[3];
    tab.serviceType = Streaming::StreamingServiceType::Hulu;
    
    tab.usernameEdit = new QLineEdit();
    tab.usernameEdit->setPlaceholderText("Email");
    tab.passwordEdit = new QLineEdit();
    tab.passwordEdit->setEchoMode(QLineEdit::Password);
    tab.passwordEdit->setPlaceholderText("Password");
    
    loginLayout->addWidget(new QLabel("Email:"));
    loginLayout->addWidget(tab.usernameEdit);
    loginLayout->addWidget(new QLabel("Password:"));
    loginLayout->addWidget(tab.passwordEdit);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    tab.loginButton = new QPushButton("Login");
    tab.logoutButton = new QPushButton("Logout");
    tab.logoutButton->setEnabled(false);
    buttonLayout->addWidget(tab.loginButton);
    buttonLayout->addWidget(tab.logoutButton);
    loginLayout->addLayout(buttonLayout);
    
    tab.statusLabel = new QLabel("Status: Not logged in");
    loginLayout->addWidget(tab.statusLabel);
    
    layout->addWidget(loginGroup);
    
    QGroupBox* searchGroup = new QGroupBox("Search");
    QVBoxLayout* searchLayout = new QVBoxLayout(searchGroup);
    
    QHBoxLayout* searchInputLayout = new QHBoxLayout();
    tab.searchEdit = new QLineEdit();
    tab.searchEdit->setPlaceholderText("Search Hulu...");
    tab.searchButton = new QPushButton("Search");
    searchInputLayout->addWidget(tab.searchEdit);
    searchInputLayout->addWidget(tab.searchButton);
    searchLayout->addLayout(searchInputLayout);
    
    layout->addWidget(searchGroup);
    
    QGroupBox* contentGroup = new QGroupBox("Content");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentGroup);
    
    tab.contentList = new QListWidget();
    contentLayout->addWidget(tab.contentList);
    
    tab.playButton = new QPushButton("Play Selected");
    tab.playButton->setEnabled(false);
    contentLayout->addWidget(tab.playButton);
    
    layout->addWidget(contentGroup);
    
    tabWidget_->addTab(huluTab_, "Hulu");
    
    connect(tab.loginButton, &QPushButton::clicked, this, &StreamingDialog::onLoginClicked);
    connect(tab.logoutButton, &QPushButton::clicked, this, &StreamingDialog::onLogoutClicked);
    connect(tab.searchButton, &QPushButton::clicked, this, &StreamingDialog::onSearchClicked);
    connect(tab.playButton, &QPushButton::clicked, this, &StreamingDialog::onPlayClicked);
    connect(tab.contentList, &QListWidget::itemClicked, this, &StreamingDialog::onContentSelected);
}

void StreamingDialog::onServiceTabChanged(int index) {
    updateServiceStatus();
}

void StreamingDialog::onLoginClicked() {
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    Streaming::StreamingCredentials creds;
    
    if (tab.usernameEdit) {
        creds.username = tab.usernameEdit->text().toStdString();
    }
    if (tab.passwordEdit) {
        creds.password = tab.passwordEdit->text().toStdString();
    }
    if (tab.apiKeyEdit) {
        creds.apiKey = tab.apiKeyEdit->text().toStdString();
    }
    
    bool success = streamingManager_.authenticateService(tab.serviceType, creds);
    
    if (success) {
        tab.statusLabel->setText("Status: Connected");
        tab.loginButton->setEnabled(false);
        tab.logoutButton->setEnabled(true);
        loadContent(tab.serviceType);
        QMessageBox::information(this, "Success", "Successfully authenticated!");
    } else {
        QMessageBox::warning(this, "Error", "Authentication failed. Please check your credentials.");
    }
}

void StreamingDialog::onLogoutClicked() {
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    streamingManager_.logoutService(tab.serviceType);
    
    tab.statusLabel->setText("Status: Not connected");
    tab.loginButton->setEnabled(true);
    tab.logoutButton->setEnabled(false);
    tab.contentList->clear();
}

void StreamingDialog::onSearchClicked() {
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    auto service = streamingManager_.getService(tab.serviceType);
    if (!service || !service->isAuthenticated()) {
        QMessageBox::warning(this, "Error", "Please login first");
        return;
    }
    
    QString query = tab.searchEdit->text();
    if (query.isEmpty()) {
        return;
    }
    
    // Perform search
    auto results = service->search(query.toStdString(), 20);
    
    tab.contentList->clear();
    for (const auto& content : results) {
        tab.contentList->addItem(QString::fromStdString(content.title));
    }
}

void StreamingDialog::onContentSelected(QListWidgetItem* item) {
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    tab.playButton->setEnabled(item != nullptr);
}

void StreamingDialog::onPlayClicked() {
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    auto selectedItem = tab.contentList->currentItem();
    if (!selectedItem) {
        return;
    }
    
    auto service = streamingManager_.getService(tab.serviceType);
    if (service) {
        QString title = selectedItem->text();
        QMessageBox::information(this, "Playback", "Starting playback: " + title);
        // Here you would integrate with a video player
    }
}

void StreamingDialog::updateServiceStatus() {
    int currentTab = tabWidget_->currentIndex();
    if (serviceTabs_.find(currentTab) == serviceTabs_.end()) {
        return;
    }
    
    auto& tab = serviceTabs_[currentTab];
    bool authenticated = streamingManager_.isServiceAuthenticated(tab.serviceType);
    
    if (authenticated) {
        tab.statusLabel->setText("Status: Connected");
        tab.loginButton->setEnabled(false);
        tab.logoutButton->setEnabled(true);
    } else {
        tab.statusLabel->setText("Status: Not connected");
        tab.loginButton->setEnabled(true);
        tab.logoutButton->setEnabled(false);
    }
}

void StreamingDialog::loadContent(Streaming::StreamingServiceType type) {
    auto service = streamingManager_.getService(type);
    if (!service) {
        return;
    }
    
    // Load trending content
    auto trending = service->getTrending(20);
    
    int currentTab = tabWidget_->currentIndex();
    auto& tab = serviceTabs_[currentTab];
    
    tab.contentList->clear();
    for (const auto& content : trending) {
        tab.contentList->addItem(QString::fromStdString(content.title));
    }
}

} // namespace GUI
} // namespace AIO
