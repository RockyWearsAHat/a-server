#include "gui/StreamingWebViewPage.h"

#include "gui/StreamingHubWidget.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtWebEngineCore/QWebEngineFullScreenRequest>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>

namespace AIO {
namespace GUI {

namespace {

int appIndex(AIO::GUI::StreamingApp app) { return static_cast<int>(app); }

QColor brandAccentForApp(AIO::GUI::StreamingApp app) {
  switch (app) {
  case AIO::GUI::StreamingApp::YouTube:
    return QColor(255, 18, 18);
  case AIO::GUI::StreamingApp::Netflix:
    return QColor(229, 9, 20);
  case AIO::GUI::StreamingApp::DisneyPlus:
    return QColor(30, 80, 240);
  case AIO::GUI::StreamingApp::Hulu:
    return QColor(28, 231, 131);
  case AIO::GUI::StreamingApp::Store:
    return QColor(212, 168, 32); // store-accent #d4a820
  }
  return QColor(100, 181, 246);
}

QString streamingStorageRoot() {
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/streaming";
  QDir().mkpath(root);
  return root;
}

} // namespace

StreamingWebViewPage::StreamingWebViewPage(QWidget *parent) : QWidget(parent) {
  setObjectName("aioStreamingWebPage");
  setFocusPolicy(Qt::StrongFocus);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  topBar_ = new QWidget(this);
  topBar_->setObjectName("aioTopBar");

  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(24, 16, 24, 16);
  barLayout->setSpacing(16);

  backButton_ = new QToolButton(topBar_);
  backButton_->setText("\u25C0  Back");
  backButton_->setAutoRaise(true);
  backButton_->setFocusPolicy(Qt::StrongFocus);
  backButton_->setObjectName("aioStreamingBarBtn");
  backButton_->setProperty("variant", "secondary");

  forwardButton_ = new QToolButton(topBar_);
  forwardButton_->setText("Forward  \u25B6");
  forwardButton_->setAutoRaise(true);
  forwardButton_->setFocusPolicy(Qt::StrongFocus);
  forwardButton_->setObjectName("aioStreamingBarBtn");
  forwardButton_->setProperty("variant", "secondary");
  forwardButton_->hide();

  appHomeButton_ = new QToolButton(topBar_);
  appHomeButton_->setText("\u2302  App Home");
  appHomeButton_->setAutoRaise(true);
  appHomeButton_->setFocusPolicy(Qt::StrongFocus);
  appHomeButton_->setObjectName("aioStreamingBarBtn");
  appHomeButton_->setProperty("variant", "secondary");

  reloadButton_ = new QToolButton(topBar_);
  reloadButton_->setText("\u21BB  Reload");
  reloadButton_->setAutoRaise(true);
  reloadButton_->setFocusPolicy(Qt::StrongFocus);
  reloadButton_->setObjectName("aioStreamingBarBtn");
  reloadButton_->setProperty("variant", "secondary");

  homeButton_ = new QToolButton(topBar_);
  homeButton_->setText("\u2190  Apps");
  homeButton_->setAutoRaise(true);
  homeButton_->setFocusPolicy(Qt::StrongFocus);
  homeButton_->setObjectName("aioStreamingBarBtn");
  homeButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("", topBar_);
  titleLabel_->setProperty("role", "subtitle");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(forwardButton_);
  barLayout->addWidget(appHomeButton_);
  barLayout->addWidget(reloadButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(titleLabel_);
  barLayout->addStretch();

  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName("aioStreamingWebStatus");
  statusLabel_->setProperty("role", "subtitle");
  statusLabel_->setContentsMargins(18, 8, 18, 8);
  statusLabel_->hide();

  errorStrip_ = new QWidget(this);
  errorStrip_->setObjectName("aioStreamingWebErrorStrip");
  auto *errorLayout = new QHBoxLayout(errorStrip_);
  errorLayout->setContentsMargins(18, 0, 18, 10);
  errorLayout->setSpacing(10);
  errorLabel_ = new QLabel(errorStrip_);
  errorLabel_->setObjectName("aioStreamingWebErrorLabel");
  errorLabel_->setProperty("role", "subtitle");
  retryButton_ = new QPushButton("Retry", errorStrip_);
  retryButton_->setFocusPolicy(Qt::StrongFocus);
  retryButton_->setObjectName("aioStreamingRetryBtn");
  retryButton_->setProperty("variant", "secondary");
  errorLayout->addWidget(errorLabel_, 1);
  errorLayout->addWidget(retryButton_);
  errorStrip_->hide();

  viewStack_ = new QStackedWidget(this);
  viewStack_->setFocusPolicy(Qt::StrongFocus);

  // --- Loading overlay page (branded splash while WebEngine initializes) ---
  loadingPage_ = new QWidget(viewStack_);
  loadingPage_->setObjectName("aioStreamingLoadOverlay");
  {
    auto *ll = new QVBoxLayout(loadingPage_);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addStretch(3);

    loadingServiceName_ = new QLabel(loadingPage_);
    loadingServiceName_->setObjectName("aioStreamingLoadTitle");
    loadingServiceName_->setAlignment(Qt::AlignCenter);
    ll->addWidget(loadingServiceName_);

    ll->addSpacing(16);

    loadingAccent_ = new QFrame(loadingPage_);
    loadingAccent_->setObjectName("aioStreamingLoadAccent");
    loadingAccent_->setFixedSize(80, 3);
    ll->addWidget(loadingAccent_, 0, Qt::AlignCenter);

    ll->addSpacing(24);

    loadingIndicator_ = new QLabel(loadingPage_);
    loadingIndicator_->setObjectName("aioStreamingLoadIndicator");
    loadingIndicator_->setAlignment(Qt::AlignCenter);
    loadingIndicator_->setText("Loading");
    ll->addWidget(loadingIndicator_);

    ll->addStretch(4);
  }
  viewStack_->addWidget(loadingPage_);

  hintLabel_ = new QLabel(this);
  hintLabel_->setObjectName("aioStreamingNavHint");
  hintLabel_->setText(
      "D-pad  .  navigate    Back  .  return    Home  .  exit streaming");
  hintLabel_->setAlignment(Qt::AlignCenter);
  hintLabel_->setContentsMargins(18, 6, 18, 6);

  hintHideTimer_ = new QTimer(this);
  hintHideTimer_->setSingleShot(true);
  hintHideTimer_->setInterval(6000);
  connect(hintHideTimer_, &QTimer::timeout, hintLabel_, &QWidget::hide);
  hintHideTimer_->start();

  root->addWidget(topBar_);
  root->addWidget(hintLabel_);
  root->addWidget(statusLabel_);
  root->addWidget(errorStrip_);
  root->addWidget(viewStack_, 1);

  connect(backButton_, &QToolButton::clicked, this, [this]() {
    if (auto *view = activeView()) {
      if (view->history()->canGoBack()) {
        view->back();
      } else {
        openAppHome();
      }
    }
  });
  connect(forwardButton_, &QToolButton::clicked, this, [this]() {
    if (auto *view = activeView(); view && view->history()->canGoForward()) {
      view->forward();
    }
  });
  connect(appHomeButton_, &QToolButton::clicked, this,
          [this]() { openAppHome(); });
  connect(reloadButton_, &QToolButton::clicked, this, [this]() {
    if (auto *view = activeView()) {
      clearLoadFailure();
      view->reload();
    }
  });
  connect(homeButton_, &QToolButton::clicked, this,
          [this]() { emit homeRequested(); });
  connect(retryButton_, &QPushButton::clicked, this, [this]() {
    if (auto *view = activeView()) {
      clearLoadFailure();
      view->reload();
      view->setFocus();
    }
  });

  dotsTimer_ = new QTimer(this);
  dotsTimer_->setInterval(400);
  connect(dotsTimer_, &QTimer::timeout, this, [this]() {
    dotsCount_ = (dotsCount_ + 1) % 4;
    loadingIndicator_->setText(
        QStringLiteral("Loading%1").arg(QString(dotsCount_, QLatin1Char('.'))));
  });

  // Install event filter on top bar buttons for D-pad Down/Enter handling.
  backButton_->installEventFilter(this);
  forwardButton_->installEventFilter(this);
  appHomeButton_->installEventFilter(this);
  reloadButton_->installEventFilter(this);
  homeButton_->installEventFilter(this);

  updateStatusText("Pick an app to open.");
  updateButtonState();
}

void StreamingWebViewPage::applyWebSettings(QWebEngineView *view) const {
  auto *profile = view->page()->profile();
  profile->setPersistentCookiesPolicy(
      QWebEngineProfile::ForcePersistentCookies);
  profile->setHttpUserAgent(
      "Mozilla/5.0 (SMART-TV; Linux; Tizen 7.0) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.6099.308 Safari/537.36");

  auto *settings = view->settings();
  settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
  settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
  settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
  settings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, true);
  settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,
                         false);
  settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
}

QString StreamingWebViewPage::titleForApp(AIO::GUI::StreamingApp app) const {
  switch (app) {
  case StreamingApp::YouTube:
    return "YouTube";
  case StreamingApp::Netflix:
    return "Netflix";
  case StreamingApp::DisneyPlus:
    return "Disney+";
  case StreamingApp::Hulu:
    return "Hulu";
  case StreamingApp::Store:
    return "Steam Store";
  }
  return "Streaming";
}

QString StreamingWebViewPage::urlForApp(AIO::GUI::StreamingApp app) const {
  switch (app) {
  case StreamingApp::YouTube:
    return "about:blank";
  case StreamingApp::Netflix:
    return "https://www.netflix.com/browse";
  case StreamingApp::DisneyPlus:
    return "https://www.disneyplus.com/home";
  case StreamingApp::Hulu:
    return "https://www.hulu.com/hub/home";
  case StreamingApp::Store:
    return "https://store.steampowered.com/";
  }
  return "https://www.youtube.com";
}

QString
StreamingWebViewPage::profileKeyForApp(AIO::GUI::StreamingApp app) const {
  switch (app) {
  case StreamingApp::YouTube:
    return "youtube-web";
  case StreamingApp::Netflix:
    return "netflix";
  case StreamingApp::DisneyPlus:
    return "disneyplus";
  case StreamingApp::Hulu:
    return "hulu";
  case StreamingApp::Store:
    return "steam-store";
  }
  return "streaming";
}

void StreamingWebViewPage::setTopBarText(const QString &text) {
  titleLabel_->setText(text);
}

QWebEngineView *StreamingWebViewPage::ensureView(AIO::GUI::StreamingApp app) {
  const int index = appIndex(app);
  if (appViews_[index]) {
    return appViews_[index];
  }

  const QString profileKey = profileKeyForApp(app);
  const QString storagePath = streamingStorageRoot() + "/" + profileKey;
  const QString cachePath = storagePath + "/cache";
  QDir().mkpath(cachePath);

  auto *profile = new QWebEngineProfile(profileKey, this);
  profile->setPersistentStoragePath(storagePath);
  profile->setCachePath(cachePath);
  profile->setPersistentCookiesPolicy(
      QWebEngineProfile::ForcePersistentCookies);

  auto *view = new QWebEngineView(viewStack_);
  auto *page = new QWebEnginePage(profile, view);
  view->setPage(page);
  view->setFocusPolicy(Qt::StrongFocus);
  view->page()->setBackgroundColor(
      QApplication::palette().color(QPalette::Window));
  applyWebSettings(view);
  view->setZoomFactor(1.25);

  connect(
      view, &QWebEngineView::titleChanged, this,
      [this, index, app](const QString &title) {
        if (currentAppIndex_ != index) {
          return;
        }
        const QString serviceName = titleForApp(app);
        const QString trimmedTitle = title.trimmed();
        setTopBarText(
            trimmedTitle.isEmpty()
                ? serviceName
                : QStringLiteral("%1  |  %2").arg(serviceName, trimmedTitle));
      });
  connect(
      view, &QWebEngineView::urlChanged, this,
      [this, index, app](const QUrl &url) {
        QSettings settings("AIOServer", "GBAEmulator");
        settings.setValue(
            QStringLiteral("streaming/%1/lastUrl").arg(profileKeyForApp(app)),
            url.toString());
        if (currentAppIndex_ == index) {
          updateStatusText(url.host().isEmpty() ? titleForApp(app)
                                                : url.host());
          updateButtonState();
        }
      });
  connect(view, &QWebEngineView::loadStarted, this, [this, index, app]() {
    if (currentAppIndex_ != index) {
      return;
    }
    clearLoadFailure();
    updateStatusText(QStringLiteral("Opening %1...").arg(titleForApp(app)));
  });
  connect(view, &QWebEngineView::loadProgress, this,
          [this, index, app](int progress) {
            if (currentAppIndex_ != index) {
              return;
            }
            updateStatusText(QStringLiteral("Loading %1... %2%")
                                 .arg(titleForApp(app))
                                 .arg(progress));
          });
  connect(view, &QWebEngineView::loadFinished, this,
          [this, index, app](bool ok) {
            if (currentAppIndex_ != index) {
              return;
            }
            if (loadingVisible_) {
              dotsTimer_->stop();
              loadingVisible_ = false;
            }
            if (!ok) {
              if (appViews_[index]) {
                viewStack_->setCurrentWidget(appViews_[index]);
              }
              showLoadFailure(
                  QStringLiteral(
                      "%1 could not finish loading. Retry, or return to Apps.")
                      .arg(titleForApp(app)));
              return;
            }
            if (appViews_[index]) {
              viewStack_->setCurrentWidget(appViews_[index]);
              appViews_[index]->setFocus();
            }
            clearLoadFailure();
            updateStatusText(QStringLiteral("%1 ready").arg(titleForApp(app)));
            updateButtonState();
          });
  connect(page, &QWebEnginePage::fullScreenRequested, this,
          [this](QWebEngineFullScreenRequest request) {
            request.accept();
            const bool hideChrome = request.toggleOn();
            topBar_->setVisible(!hideChrome);
            hintLabel_->setVisible(!hideChrome);
            if (!hideChrome && errorLabel_ && !errorLabel_->text().isEmpty()) {
              errorStrip_->show();
            } else if (hideChrome) {
              errorStrip_->hide();
            }
          });

  view->installEventFilter(this);
  view->page()->installEventFilter(this);
  if (auto *proxy = view->focusProxy()) {
    proxy->installEventFilter(this);
  }
  viewStack_->addWidget(view);

  appViews_[index] = view;
  appProfiles_[index] = profile;
  return view;
}

QWebEngineView *StreamingWebViewPage::activeView() const {
  if (currentAppIndex_ < 0 ||
      currentAppIndex_ >= static_cast<int>(appViews_.size())) {
    return nullptr;
  }
  return appViews_[currentAppIndex_];
}

void StreamingWebViewPage::openAppHome() {
  if (currentAppIndex_ < 0) {
    return;
  }
  const auto app = static_cast<AIO::GUI::StreamingApp>(currentAppIndex_);
  if (auto *view = activeView()) {
    clearLoadFailure();
    view->setUrl(QUrl(urlForApp(app)));
    view->setFocus();
  }
}

void StreamingWebViewPage::updateStatusText(const QString &text) {
  statusLabel_->setText(text);
}

void StreamingWebViewPage::updateButtonState() {
  auto *view = activeView();
  const bool hasView = (view != nullptr);
  backButton_->setEnabled(hasView && view->history()->canGoBack());
  const bool canFwd = hasView && view->history()->canGoForward();
  forwardButton_->setVisible(canFwd);
  forwardButton_->setEnabled(canFwd);
  appHomeButton_->setEnabled(hasView);
  reloadButton_->setEnabled(hasView);
}

void StreamingWebViewPage::clearLoadFailure() {
  if (errorLabel_) {
    errorLabel_->clear();
  }
  if (errorStrip_) {
    errorStrip_->hide();
  }
}

void StreamingWebViewPage::showLoadFailure(const QString &text) {
  if (!errorLabel_ || !errorStrip_) {
    return;
  }
  errorLabel_->setText(text);
  errorStrip_->show();
  updateStatusText("Load failed");
}

void StreamingWebViewPage::openApp(AIO::GUI::StreamingApp app) {
  currentAppIndex_ = appIndex(app);
  QSettings settings("AIOServer", "GBAEmulator");
  settings.setValue(QStringLiteral("streaming/lastApp"), currentAppIndex_);

  auto *view = ensureView(app);
  if (!view) {
    return;
  }

  viewStack_->setCurrentWidget(view);
  setTopBarText(titleForApp(app));
  clearLoadFailure();
  updateStatusText(QStringLiteral("Opening %1...").arg(titleForApp(app)));

  if (!view->url().isValid() || view->url().isEmpty() ||
      view->url().toString() == "about:blank") {
    loadingServiceName_->setText(titleForApp(app));
    loadingAccent_->setStyleSheet(
        QStringLiteral("background-color: %1; border-radius: 2px;")
            .arg(brandAccentForApp(app).name()));
    viewStack_->setCurrentWidget(loadingPage_);
    dotsCount_ = 0;
    dotsTimer_->start();
    loadingVisible_ = true;

    const QString savedUrl = settings
                                 .value(QStringLiteral("streaming/%1/lastUrl")
                                            .arg(profileKeyForApp(app)),
                                        urlForApp(app))
                                 .toString();
    view->setUrl(QUrl(savedUrl));
  } else {
    viewStack_->setCurrentWidget(view);
    loadingVisible_ = false;
  }

  view->setFocus();
  hintLabel_->show();
  if (hintHideTimer_) {
    hintHideTimer_->start();
  }
  updateButtonState();
}

bool StreamingWebViewPage::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::FocusIn) {
    updateButtonState();
  }

  // Intercept keys from WebEngineView (check view, page, and child widgets
  // such as the internal render widget / focus proxy).
  auto *view = activeView();
  const bool isWebViewEvent =
      view && (watched == view || watched == view->page() ||
               (qobject_cast<QWidget *>(watched) &&
                view->isAncestorOf(qobject_cast<QWidget *>(watched))));

  if (isWebViewEvent && event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);

    if (keyEvent->key() == Qt::Key_Up &&
        keyEvent->modifiers() == Qt::NoModifier) {
      backButton_->setFocus();
      return true;
    }

    if (handleKeyPress(keyEvent)) {
      return true;
    }
  }

  // Handle D-pad keys on top bar buttons.
  if (event->type() == QEvent::KeyPress) {
    auto *btn = qobject_cast<QToolButton *>(watched);
    if (btn && btn->parent() == topBar_) {
      auto *keyEvent = static_cast<QKeyEvent *>(event);

      if (keyEvent->key() == Qt::Key_Down) {
        if (auto *v = activeView()) {
          v->setFocus();
          return true;
        }
      }

      if (keyEvent->key() == Qt::Key_Return ||
          keyEvent->key() == Qt::Key_Enter ||
          keyEvent->key() == Qt::Key_Space) {
        btn->click();
        return true;
      }

      // Left/Right arrows move between top bar buttons, skipping disabled.
      if (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right) {
        const QList<QToolButton *> buttons = {backButton_, forwardButton_,
                                              appHomeButton_, reloadButton_,
                                              homeButton_};
        const int idx = buttons.indexOf(btn);
        if (idx < 0) {
          return false;
        }
        const int step = (keyEvent->key() == Qt::Key_Right) ? 1 : -1;
        for (int i = idx + step; i >= 0 && i < buttons.size(); i += step) {
          if (buttons[i]->isEnabled() && buttons[i]->isVisible()) {
            buttons[i]->setFocus();
            return true;
          }
        }
        return true; // consume even if no target found
      }
    }
  }

  return QWidget::eventFilter(watched, event);
}

bool StreamingWebViewPage::handleKeyPress(QKeyEvent *event) {
  if (!event) {
    return false;
  }

  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
    if (auto *view = activeView(); view && view->history()->canGoBack()) {
      view->back();
    } else {
      emit homeRequested();
    }
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_F5 ||
      ((event->modifiers() & Qt::ControlModifier) &&
       event->key() == Qt::Key_R)) {
    if (auto *view = activeView()) {
      clearLoadFailure();
      view->reload();
    }
    event->accept();
    return true;
  }

  if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Left) {
    if (auto *view = activeView(); view && view->history()->canGoBack()) {
      view->back();
    }
    event->accept();
    return true;
  }

  if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Right) {
    if (auto *view = activeView(); view && view->history()->canGoForward()) {
      view->forward();
    }
    event->accept();
    return true;
  }

  return false;
}

void StreamingWebViewPage::keyPressEvent(QKeyEvent *event) {
  if (handleKeyPress(event)) {
    return;
  }

  QWidget::keyPressEvent(event);
}

} // namespace GUI
} // namespace AIO
