#include "gui/ScreenMirrorPage.h"
#include "screenmirror/MirrorSessionManager.h"

#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>

namespace AIO::GUI {

using SessionState = AIO::ScreenMirror::MirrorSessionManager::SessionState;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScreenMirrorPage::ScreenMirrorPage(QWidget *parent)
    : QWidget(parent),
      session_(
          std::make_unique<AIO::ScreenMirror::MirrorSessionManager>(this)) {
  setObjectName(QStringLiteral("aioScreenMirrorPage"));
  setFocusPolicy(Qt::StrongFocus);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  stack_ = new QStackedWidget(this);
  root->addWidget(stack_);

  setupWaitingUi();
  setupMirroringUi();

  stack_->setCurrentWidget(waitingPage_);

  // Animated dots timer for the waiting state.
  dotsTimer_ = new QTimer(this);
  dotsTimer_->setInterval(500);
  connect(dotsTimer_, &QTimer::timeout, this, &ScreenMirrorPage::animateDots);

  // Session state wiring.
  connect(session_.get(),
          &AIO::ScreenMirror::MirrorSessionManager::sessionStateChanged, this,
          &ScreenMirrorPage::onSessionStateChanged);
  connect(session_.get(),
          &AIO::ScreenMirror::MirrorSessionManager::frameReceived, this,
          &ScreenMirrorPage::onFrameReceived);
  connect(session_.get(),
          &AIO::ScreenMirror::MirrorSessionManager::clientDeviceNameChanged,
          this, [this](const QString &name) {
            if (!name.isEmpty())
              subtitleLabel_->setText(
                  QStringLiteral("Connecting to %1…").arg(name));
          });

  // Auto-start AirPlay receiver so the device is always discoverable,
  // like a real Apple TV.
  session_->startReceiving();
}

ScreenMirrorPage::~ScreenMirrorPage() {
  // Disconnect before session_ is destroyed so its destructor signals
  // don't call back into this partially-destroyed widget (SIGSEGV).
  if (session_)
    session_->disconnect(this);
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void ScreenMirrorPage::setupWaitingUi() {
  waitingPage_ = new QWidget(this);
  waitingPage_->setObjectName(QStringLiteral("screenMirrorWaiting"));

  auto *outer = new QVBoxLayout(waitingPage_);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  // Centered content area.
  auto *center = new QWidget(waitingPage_);
  center->setObjectName(QStringLiteral("screenMirrorCenter"));
  auto *cl = new QVBoxLayout(center);
  cl->setAlignment(Qt::AlignCenter);
  cl->setSpacing(12);

  // AirPlay-style icon — drawn as a styled label.
  iconLabel_ = new QLabel(center);
  iconLabel_->setObjectName(QStringLiteral("screenMirrorIcon"));
  iconLabel_->setAlignment(Qt::AlignCenter);
  iconLabel_->setFixedSize(180, 180);
  // Paint an AirPlay-style icon (screen with wireless arcs).
  {
    QPixmap pix(180, 180);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor accent(100, 181, 246); // #64b5f6 — design-system accent

    // Wireless arcs above screen — center at (90, 48), radii 16/28/40.
    // Topmost point: 48 − 40 = 8px — comfortably inside the pixmap.
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 3; ++i) {
      const qreal r = 16.0 + i * 12.0;
      const int alpha = 255 - i * 55;
      p.setPen(QPen(QColor(100, 181, 246, alpha), 3.0));
      p.drawArc(QRectF(90 - r, 48 - r, r * 2, r * 2), 30 * 16, 120 * 16);
    }

    // Screen body.
    QPen pen(accent, 3.5);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(26, 70, 128, 64), 8, 8);

    // Screen stand.
    p.drawLine(QPointF(68, 134), QPointF(68, 148));
    p.drawLine(QPointF(112, 134), QPointF(112, 148));
    p.drawLine(QPointF(54, 148), QPointF(126, 148));

    p.end();
    iconLabel_->setPixmap(pix);
  }
  cl->addWidget(iconLabel_, 0, Qt::AlignCenter);

  cl->addSpacing(8);

  // Title.
  titleLabel_ = new QLabel(QStringLiteral("Screen Mirror"), center);
  titleLabel_->setObjectName(QStringLiteral("screenMirrorTitle"));
  titleLabel_->setProperty("role", QStringLiteral("title"));
  titleLabel_->setAlignment(Qt::AlignCenter);
  cl->addWidget(titleLabel_);

  // Subtitle / status.
  subtitleLabel_ = new QLabel(QStringLiteral("Ready to receive"), center);
  subtitleLabel_->setObjectName(QStringLiteral("screenMirrorSubtitle"));
  subtitleLabel_->setProperty("role", QStringLiteral("subtitle"));
  subtitleLabel_->setAlignment(Qt::AlignCenter);
  cl->addWidget(subtitleLabel_);

  cl->addSpacing(24);

  // IP address info.
  ipLabel_ = new QLabel(center);
  ipLabel_->setObjectName(QStringLiteral("screenMirrorIp"));
  ipLabel_->setAlignment(Qt::AlignCenter);
  cl->addWidget(ipLabel_);

  cl->addSpacing(16);

  // Instructions.
  instructionLabel_ = new QLabel(center);
  instructionLabel_->setObjectName(QStringLiteral("screenMirrorInstruction"));
  instructionLabel_->setAlignment(Qt::AlignCenter);
  instructionLabel_->setWordWrap(true);
  instructionLabel_->setText(
      QStringLiteral("On your iPhone, iPad, or Mac, open Control Center\n"
                     "and tap Screen Mirroring. Select this device to begin."));
  cl->addWidget(instructionLabel_);

  cl->addSpacing(20);

  // Animated waiting dots.
  dotsLabel_ = new QLabel(QStringLiteral("Waiting for connection"), center);
  dotsLabel_->setObjectName(QStringLiteral("screenMirrorDots"));
  dotsLabel_->setProperty("role", QStringLiteral("subtitle"));
  dotsLabel_->setAlignment(Qt::AlignCenter);
  cl->addWidget(dotsLabel_);

  outer->addStretch(1);
  outer->addWidget(center, 0, Qt::AlignCenter);
  outer->addStretch(1);

  // Bottom bar with back button.
  auto *bottomBar = new QWidget(waitingPage_);
  bottomBar->setObjectName(QStringLiteral("screenMirrorBottomBar"));
  auto *bbLayout = new QHBoxLayout(bottomBar);
  bbLayout->setContentsMargins(32, 12, 32, 24);

  backBtn_ = new QPushButton(QStringLiteral("Back"), bottomBar);
  backBtn_->setObjectName(QStringLiteral("screenMirrorBackBtn"));
  backBtn_->setFocusPolicy(Qt::StrongFocus);
  connect(backBtn_, &QPushButton::clicked, this,
          &ScreenMirrorPage::homeRequested);

  bbLayout->addWidget(backBtn_);
  bbLayout->addStretch(1);
  outer->addWidget(bottomBar);

  stack_->addWidget(waitingPage_);
}

void ScreenMirrorPage::setupMirroringUi() {
  mirrorPage_ = new QWidget(this);
  mirrorPage_->setObjectName(QStringLiteral("screenMirrorActive"));

  auto *ml = new QVBoxLayout(mirrorPage_);
  ml->setContentsMargins(0, 0, 0, 0);
  ml->setSpacing(0);

  videoLabel_ = new QLabel(mirrorPage_);
  videoLabel_->setObjectName(QStringLiteral("screenMirrorVideo"));
  videoLabel_->setAlignment(Qt::AlignCenter);
  videoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  ml->addWidget(videoLabel_);

  // Floating overlay label for connection info (shown briefly).
  overlayLabel_ = new QLabel(mirrorPage_);
  overlayLabel_->setObjectName(QStringLiteral("screenMirrorOverlay"));
  overlayLabel_->setAlignment(Qt::AlignCenter);
  overlayLabel_->setVisible(false);

  stack_->addWidget(mirrorPage_);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void ScreenMirrorPage::onSessionStateChanged() {
  switch (session_->sessionState()) {
  case SessionState::Idle:
  case SessionState::Waiting:
    showWaiting();
    break;
  case SessionState::Connecting:
    showConnecting();
    break;
  case SessionState::Mirroring:
    showMirroring();
    break;
  case SessionState::Error:
    showError();
    break;
  }
}

void ScreenMirrorPage::showWaiting() {
  stack_->setCurrentWidget(waitingPage_);
  titleLabel_->setText(QStringLiteral("Screen Mirror"));
  subtitleLabel_->setProperty("state", QString());
  subtitleLabel_->style()->unpolish(subtitleLabel_);
  subtitleLabel_->style()->polish(subtitleLabel_);
  subtitleLabel_->setText(QStringLiteral("Ready to receive"));
  dotsLabel_->setVisible(true);
  dotsTimer_->start();
  updateNetworkInfo();
}

void ScreenMirrorPage::showConnecting() {
  stack_->setCurrentWidget(waitingPage_);
  subtitleLabel_->setProperty("state", QString());
  subtitleLabel_->style()->unpolish(subtitleLabel_);
  subtitleLabel_->style()->polish(subtitleLabel_);
  const QString client = session_->clientDeviceName();
  subtitleLabel_->setText(
      QStringLiteral("Connecting to %1…")
          .arg(client.isEmpty() ? QStringLiteral("device") : client));
  dotsLabel_->setText(QStringLiteral("Establishing connection"));
}

void ScreenMirrorPage::showMirroring() {
  stack_->setCurrentWidget(mirrorPage_);
  dotsTimer_->stop();

  // Show overlay briefly.
  const QString client = session_->clientDeviceName();
  overlayLabel_->setText(
      QStringLiteral("Mirroring from %1")
          .arg(client.isEmpty() ? QStringLiteral("device") : client));
  overlayLabel_->setVisible(true);
  QTimer::singleShot(3000, overlayLabel_,
                     [this]() { overlayLabel_->setVisible(false); });
}

void ScreenMirrorPage::showError() {
  stack_->setCurrentWidget(waitingPage_);
  subtitleLabel_->setProperty("state", QStringLiteral("error"));
  subtitleLabel_->style()->unpolish(subtitleLabel_);
  subtitleLabel_->style()->polish(subtitleLabel_);
  subtitleLabel_->setText(session_->lastError());
  dotsLabel_->setText(QStringLiteral("Press Back to return home"));
  dotsTimer_->stop();
}

// ---------------------------------------------------------------------------
// Frame display
// ---------------------------------------------------------------------------

void ScreenMirrorPage::onFrameReceived(const QImage &frame) {
  lastFrame_ = frame;
  scaleMirrorFrame();
}

void ScreenMirrorPage::scaleMirrorFrame() {
  if (lastFrame_.isNull() || !videoLabel_)
    return;
  const QSize target = videoLabel_->size();
  if (target.isEmpty())
    return;
  const QPixmap scaled =
      QPixmap::fromImage(lastFrame_)
          .scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  videoLabel_->setPixmap(scaled);
}

// ---------------------------------------------------------------------------
// Network info
// ---------------------------------------------------------------------------

void ScreenMirrorPage::updateNetworkInfo() {
  const QString ip = session_->localIpAddress();
  const QString name = session_->deviceName();
  ipLabel_->setText(QStringLiteral("<b>%1</b> &nbsp;·&nbsp; %2").arg(name, ip));
}

// ---------------------------------------------------------------------------
// Animated dots
// ---------------------------------------------------------------------------

void ScreenMirrorPage::animateDots() {
  dotCount_ = (dotCount_ + 1) % 4;
  QString dots;
  for (int i = 0; i < dotCount_; ++i)
    dots += QChar(0x2022); // bullet
  dotsLabel_->setText(QStringLiteral("Waiting for connection %1").arg(dots));
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void ScreenMirrorPage::keyPressEvent(QKeyEvent *event) {
  switch (event->key()) {
  case Qt::Key_Escape:
  case Qt::Key_Backspace:
    session_->stopReceiving();
    emit homeRequested();
    event->accept();
    return;
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

void ScreenMirrorPage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  dotsTimer_->start();
  updateNetworkInfo();
}

void ScreenMirrorPage::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  dotsTimer_->stop();
}

void ScreenMirrorPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (stack_->currentWidget() == mirrorPage_)
    scaleMirrorFrame();
}

} // namespace AIO::GUI
