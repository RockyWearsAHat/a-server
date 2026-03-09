#pragma once

#include <array>

#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
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
  QWidget *errorStrip_ = nullptr;
  QPushButton *retryButton_ = nullptr;
  QToolButton *backButton_ = nullptr;
  QToolButton *appHomeButton_ = nullptr;
  QToolButton *homeButton_ = nullptr;
  QToolButton *reloadButton_ = nullptr;

  std::array<QWebEngineView *, 4> appViews_{};
  std::array<QWebEngineProfile *, 4> appProfiles_{};
  int currentAppIndex_ = -1;
};

} // namespace GUI
} // namespace AIO
