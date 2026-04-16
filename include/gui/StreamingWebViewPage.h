#pragma once

#include <array>

#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class QWebEngineProfile;
class QWebEngineView;

namespace AIO {
namespace GUI {

enum class StreamingApp;

class StreamingWebViewPage final : public QWidget {
  Q_OBJECT

public:
  explicit StreamingWebViewPage(QWidget *parent = nullptr);

  void openApp(AIO::GUI::StreamingApp app);
  void openSteamStore(const QString &steamAppId);

signals:
  void homeRequested();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void applyWebSettings(QWebEngineView *view) const;
  bool handleKeyPress(QKeyEvent *event);
  void setTopBarText(const QString &text);
  QString urlForApp(AIO::GUI::StreamingApp app) const;
  QString titleForApp(AIO::GUI::StreamingApp app) const;
  QString profileKeyForApp(AIO::GUI::StreamingApp app) const;
  QWebEngineView *ensureView(AIO::GUI::StreamingApp app);
  QWebEngineView *activeView() const;
  void openAppHome();
  void updateStatusText(const QString &text);
  void updateButtonState();
  void clearLoadFailure();
  void showLoadFailure(const QString &text);

  QStackedWidget *viewStack_ = nullptr;
  QWidget *topBar_ = nullptr;
  QLabel *titleLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QLabel *errorLabel_ = nullptr;
  QLabel *hintLabel_ = nullptr;
  QWidget *errorStrip_ = nullptr;
  QPushButton *retryButton_ = nullptr;
  QToolButton *backButton_ = nullptr;
  QToolButton *forwardButton_ = nullptr;
  QToolButton *appHomeButton_ = nullptr;
  QToolButton *homeButton_ = nullptr;
  QToolButton *reloadButton_ = nullptr;

  QWidget *loadingPage_ = nullptr;
  QLabel *loadingServiceName_ = nullptr;
  QFrame *loadingAccent_ = nullptr;
  QLabel *loadingIndicator_ = nullptr;
  QTimer *dotsTimer_ = nullptr;
  QTimer *hintHideTimer_ = nullptr;
  int dotsCount_ = 0;
  bool loadingVisible_ = false;
  QString steamStoreHomeUrl_;

  std::array<QWebEngineView *, 5> appViews_{};
  std::array<QWebEngineProfile *, 5> appProfiles_{};
  int currentAppIndex_ = -1;
};

} // namespace GUI
} // namespace AIO
