#ifndef STREAMING_DIALOG_H
#define STREAMING_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "streaming/StreamingService.h"
#include "streaming/StreamingManager.h"

namespace AIO {
namespace GUI {

class StreamingDialog : public QDialog {
    Q_OBJECT

public:
    explicit StreamingDialog(QWidget* parent = nullptr);
    ~StreamingDialog() override = default;

private slots:
    void onServiceTabChanged(int index);
    void onLoginClicked();
    void onLogoutClicked();
    void onSearchClicked();
    void onContentSelected(QListWidgetItem* item);
    void onPlayClicked();
    
private:
    void setupUI();
    void setupYouTubeTab();
    void setupNetflixTab();
    void setupDisneyPlusTab();
    void setupHuluTab();
    
    void updateServiceStatus();
    void loadContent(Streaming::StreamingServiceType type);
    
    // UI Components
    QTabWidget* tabWidget_;
    
    // Service tabs
    QWidget* youtubeTab_;
    QWidget* netflixTab_;
    QWidget* disneyPlusTab_;
    QWidget* huluTab_;
    
    // Common controls per tab
    struct ServiceTab {
        QLineEdit* usernameEdit;
        QLineEdit* passwordEdit;
        QLineEdit* apiKeyEdit;
        QPushButton* loginButton;
        QPushButton* logoutButton;
        QLabel* statusLabel;
        QLineEdit* searchEdit;
        QPushButton* searchButton;
        QListWidget* contentList;
        QPushButton* playButton;
        Streaming::StreamingServiceType serviceType;
    };
    
    std::map<int, ServiceTab> serviceTabs_;
    Streaming::StreamingManager& streamingManager_;
};

} // namespace GUI
} // namespace AIO

#endif // STREAMING_DIALOG_H
