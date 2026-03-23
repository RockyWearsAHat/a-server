#pragma once

#include <QImage>
#include <QWidget>

#include <memory>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace AIO::ScreenMirror {
class MirrorSessionManager;
}

namespace AIO::GUI {

/// Full-screen TV page for the Screen Mirror / AirPlay receiver.
/// Shows a waiting state with connection instructions, transitions to
/// full-screen video when a client begins mirroring.
class ScreenMirrorPage final : public QWidget {
  Q_OBJECT

public:
  explicit ScreenMirrorPage(QWidget *parent = nullptr);
  ~ScreenMirrorPage() override;

  QPushButton *backButton() const { return backBtn_; }

signals:
  void homeRequested();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onSessionStateChanged();
  void onFrameReceived(const QImage &frame);
  void animateDots();

private:
  void setupWaitingUi();
  void setupMirroringUi();
  void showWaiting();
  void showConnecting();
  void showMirroring();
  void showError();
  void updateNetworkInfo();
  void scaleMirrorFrame();

  std::unique_ptr<AIO::ScreenMirror::MirrorSessionManager> session_;

  QStackedWidget *stack_ = nullptr;

  // Waiting page widgets
  QWidget *waitingPage_ = nullptr;
  QLabel *iconLabel_ = nullptr;
  QLabel *titleLabel_ = nullptr;
  QLabel *subtitleLabel_ = nullptr;
  QLabel *ipLabel_ = nullptr;
  QLabel *instructionLabel_ = nullptr;
  QLabel *dotsLabel_ = nullptr;
  QPushButton *backBtn_ = nullptr;

  // Mirroring page widgets
  QWidget *mirrorPage_ = nullptr;
  QLabel *videoLabel_ = nullptr;
  QLabel *overlayLabel_ = nullptr;

  QTimer *dotsTimer_ = nullptr;
  int dotCount_ = 0;

  QImage lastFrame_;
};

} // namespace AIO::GUI
