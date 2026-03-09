#include "gui/StreamingWebViewPage.h"

#include "gui/StreamingHubWidget.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
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

QString streamingStorageRoot() {
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/streaming";
  QDir().mkpath(root);
  return root;
}

} // namespace

StreamingWebViewPage::StreamingWebViewPage(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  topBar_ = new QWidget(this);
  topBar_->setObjectName("aioTopBar");

  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(12, 10, 12, 10);
  barLayout->setSpacing(10);

  backButton_ = new QToolButton(topBar_);
  backButton_->setText("Back");
  backButton_->setAutoRaise(true);
  backButton_->setFocusPolicy(Qt::NoFocus);
  backButton_->setProperty("variant", "secondary");

  appHomeButton_ = new QToolButton(topBar_);
  appHomeButton_->setText("App Home");
  appHomeButton_->setAutoRaise(true);
  appHomeButton_->setFocusPolicy(Qt::NoFocus);
  appHomeButton_->setProperty("variant", "secondary");

  reloadButton_ = new QToolButton(topBar_);
  reloadButton_->setText("Reload");
  reloadButton_->setAutoRaise(true);
  reloadButton_->setFocusPolicy(Qt::NoFocus);
  reloadButton_->setProperty("variant", "secondary");

  homeButton_ = new QToolButton(topBar_);
  homeButton_->setText("Apps");
  homeButton_->setAutoRaise(true);
  homeButton_->setFocusPolicy(Qt::NoFocus);
  homeButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("", topBar_);
  titleLabel_->setProperty("role", "subtitle");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(appHomeButton_);
  barLayout->addWidget(reloadButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(titleLabel_);
  barLayout->addStretch();

  statusLabel_ = new QLabel(this);
  statusLabel_->setProperty("role", "subtitle");
  statusLabel_->setContentsMargins(18, 8, 18, 8);

  errorStrip_ = new QWidget(this);
  auto *errorLayout = new QHBoxLayout(errorStrip_);
  errorLayout->setContentsMargins(18, 0, 18, 10);
  errorLayout->setSpacing(10);
  errorLabel_ = new QLabel(errorStrip_);
  errorLabel_->setProperty("role", "subtitle");
  retryButton_ = new QPushButton("Retry", errorStrip_);
  retryButton_->setFocusPolicy(Qt::NoFocus);
  retryButton_->setProperty("variant", "secondary");
  errorLayout->addWidget(errorLabel_, 1);
  errorLayout->addWidget(retryButton_);
  errorStrip_->hide();

  viewStack_ = new QStackedWidget(this);
  viewStack_->setFocusPolicy(Qt::StrongFocus);

  root->addWidget(topBar_);
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

  updateStatusText("Pick an app to open.");
  updateButtonState();
}

void StreamingWebViewPage::applyWebSettings(QWebEngineView *view) const {
  auto *profile = view->page()->profile();
  profile->setPersistentCookiesPolicy(
      QWebEngineProfile::ForcePersistentCookies);
  profile->setHttpUserAgent(
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0 Safari/537.36 AIOStreaming/1.0");

  auto *settings = view->settings();
  settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
  settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
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
            if (!ok) {
              showLoadFailure(
                  QStringLiteral(
                      "%1 could not finish loading. Retry, or return to Apps.")
                      .arg(titleForApp(app)));
              return;
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
            statusLabel_->setVisible(!hideChrome);
            if (!hideChrome && errorLabel_ && !errorLabel_->text().isEmpty()) {
              errorStrip_->show();
            } else if (hideChrome) {
              errorStrip_->hide();
            }
          });

  view->installEventFilter(this);
  view->page()->installEventFilter(this);
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
    QSettings settings("AIOServer", "GBAEmulator");
    const QString savedUrl = settings
                                 .value(QStringLiteral("streaming/%1/lastUrl")
                                            .arg(profileKeyForApp(app)),
                                        urlForApp(app))
                                 .toString();
    view->setUrl(QUrl(savedUrl));
  }

  view->setFocus();
  updateButtonState();
}

bool StreamingWebViewPage::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::FocusIn) {
    updateButtonState();
  }

  if ((watched == activeView() ||
       (activeView() && watched == activeView()->page())) &&
      event->type() == QEvent::KeyPress) {
    if (handleKeyPress(static_cast<QKeyEvent *>(event))) {
      return true;
    }
  }

  return QWidget::eventFilter(watched, event);
}

bool StreamingWebViewPage::handleKeyPress(QKeyEvent *event) {
  if (!event) {
    return false;
  }

  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
    emit homeRequested();
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
