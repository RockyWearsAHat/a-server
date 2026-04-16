#include "gui/SteamAuthDialog.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedLayout>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace AIO::GUI {

SteamAuthDialog::SteamAuthDialog(int localPort, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint),
      localPort_(localPort) {
  setObjectName(QStringLiteral("aioSteamDialog"));
  setAttribute(Qt::WA_StyledBackground, true);
  setModal(true);
  resize(1060, 740);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ── Custom title bar ──────────────────────────────────────────────────────
  auto *titleBar = new QWidget(this);
  titleBar->setObjectName(QStringLiteral("aioSteamDialogTitleBar"));
  titleBar->setAttribute(Qt::WA_StyledBackground, true);
  titleBar->setFixedHeight(52);

  auto *titleLay = new QHBoxLayout(titleBar);
  titleLay->setContentsMargins(20, 0, 12, 0);
  titleLay->setSpacing(12);

  // Steam-style icon
  auto *iconLbl = new QLabel(titleBar);
  iconLbl->setFixedSize(28, 28);
  {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x1a, 0x9f, 0xff, 230));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(0, 0, 28, 28));
    p.setPen(QPen(Qt::white, 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(6, 6, 16, 16));
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(10, 10, 8, 8));
    iconLbl->setPixmap(pm);
  }
  titleLay->addWidget(iconLbl);

  auto *titleLbl = new QLabel(QStringLiteral("Sign in to Steam"), titleBar);
  titleLbl->setObjectName(QStringLiteral("aioSteamDialogTitle"));
  titleLay->addWidget(titleLbl);
  titleLay->addStretch();

  auto *closeBtn = new QPushButton(QStringLiteral("\u2715"), titleBar);
  closeBtn->setObjectName(QStringLiteral("aioSteamDialogClose"));
  closeBtn->setFixedSize(40, 40);
  closeBtn->setCursor(Qt::PointingHandCursor);
  closeBtn->setFocusPolicy(Qt::NoFocus);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
  titleLay->addWidget(closeBtn);
  root->addWidget(titleBar);

  // ── Separator ─────────────────────────────────────────────────────────────
  auto *sep = new QFrame(this);
  sep->setObjectName(QStringLiteral("aioSteamDialogSep"));
  sep->setFrameShape(QFrame::HLine);
  sep->setFixedHeight(1);
  root->addWidget(sep);

  // ── Web area: webview + loading overlay stacked ───────────────────────────
  auto *webContainer = new QWidget(this);
  root->addWidget(webContainer, 1);

  // QStackedLayout::StackAll shows all layers simultaneously; the last-added
  // widget renders on top. Hiding the overlay reveals the webview beneath it.
  auto *stackLay = new QStackedLayout(webContainer);
  stackLay->setStackingMode(QStackedLayout::StackAll);
  stackLay->setContentsMargins(0, 0, 0, 0);
  stackLay->setSpacing(0);

  webView_ = new QWebEngineView(webContainer);
  stackLay->addWidget(webView_); // index 0 — bottom

  loadingOverlay_ = new QWidget(webContainer);
  loadingOverlay_->setObjectName(QStringLiteral("aioSteamDialogLoading"));
  loadingOverlay_->setAttribute(Qt::WA_StyledBackground, true);
  auto *overlayLay = new QVBoxLayout(loadingOverlay_);
  overlayLay->setAlignment(Qt::AlignCenter);
  overlayLay->setSpacing(12);
  auto *loadText =
      new QLabel(QStringLiteral("Loading Steam\u2026"), loadingOverlay_);
  loadText->setObjectName(QStringLiteral("aioSteamDialogLoadingText"));
  loadText->setAlignment(Qt::AlignCenter);
  overlayLay->addWidget(loadText);
  stackLay->addWidget(loadingOverlay_); // index 1 — on top, covers webview

  connect(webView_, &QWebEngineView::urlChanged, this,
          &SteamAuthDialog::onUrlChanged);
  connect(webView_, &QWebEngineView::loadFinished, this,
          &SteamAuthDialog::onLoadFinished);

  const QUrl startUrl = QUrl(
      QStringLiteral("http://127.0.0.1:%1/steam/auth/start").arg(localPort_));
  webView_->load(startUrl);
}

void SteamAuthDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);

  // Center over the parent window
  if (const QWidget *p = parentWidget()) {
    const QPoint screenCenter =
        p->mapToGlobal(QPoint(p->width() / 2, p->height() / 2));
    move(screenCenter.x() - width() / 2, screenCenter.y() - height() / 2);
  }

  // The WebEngine view must receive focus for mouse and keyboard input to work.
  webView_->setFocus(Qt::ActiveWindowFocusReason);
}

void SteamAuthDialog::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    reject();
    return;
  }
  // Pass all other key events (arrows, Tab, Enter) directly to the web view
  // so the Steam page is fully navigable from a TV remote or keyboard.
  QApplication::sendEvent(webView_, event);
}

void SteamAuthDialog::onLoadFinished(bool /*ok*/) {
  // Reveal the webview by hiding the loading overlay.
  if (loadingOverlay_)
    loadingOverlay_->hide();
  // Re-assert focus so the webview receives mouse and key input immediately.
  webView_->setFocus(Qt::OtherFocusReason);
}

void SteamAuthDialog::onUrlChanged(const QUrl &url) {
  const QString host = url.host();
  const QString path = url.path();

  if (host == QStringLiteral("127.0.0.1") &&
      path.startsWith(QStringLiteral("/steam/auth/callback"))) {
    startPolling();
  }
}

void SteamAuthDialog::startPolling() {
  // Poll /api/steam/auth/status up to 10 times (5 s max).
  auto *nam = new QNetworkAccessManager(this);
  auto *timer = new QTimer(this);
  auto attempts = std::make_shared<int>(0);

  timer->setInterval(500);
  connect(timer, &QTimer::timeout, this, [this, nam, timer, attempts]() {
    ++(*attempts);
    if (*attempts > 10) {
      timer->stop();
      return;
    }

    const QUrl statusUrl =
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/steam/auth/status")
                 .arg(localPort_));
    QNetworkRequest req{statusUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader, "AIOServer/1.0");
    auto *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, timer, attempts]() {
              const QByteArray data = reply->readAll();
              reply->deleteLater();

              const auto doc = QJsonDocument::fromJson(data);
              if (!doc.isObject())
                return;

              const QJsonObject obj = doc.object();
              if (!obj.value(QStringLiteral("authenticated")).toBool())
                return;

              const QString id =
                  obj.value(QStringLiteral("steamId")).toString();
              if (id.isEmpty())
                return;

              timer->stop();
              resolvedSteamId_ = id;
              emit authComplete(id);
              accept();
            });
  });
  timer->start();
}

} // namespace AIO::GUI
