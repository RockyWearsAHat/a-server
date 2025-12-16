#include "gui/YouTubePlayerPage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <QWebEngineView>
#include <QWebEngineHistory>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineCore/QWebEngineFullScreenRequest>

#include "input/InputManager.h"

namespace AIO {
namespace GUI {

YouTubePlayerPage::YouTubePlayerPage(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    topBar_ = new QWidget(this);
    topBar_->setObjectName("aioTopBar");

    auto* barLayout = new QHBoxLayout(topBar_);
    barLayout->setContentsMargins(12, 10, 12, 10);
    barLayout->setSpacing(10);

    backButton_ = new QToolButton(topBar_);
    backButton_->setText("Back");
    backButton_->setAutoRaise(true);
    backButton_->setFocusPolicy(Qt::NoFocus);
    backButton_->setProperty("variant", "secondary");

    homeButton_ = new QToolButton(topBar_);
    homeButton_->setText("Home");
    homeButton_->setAutoRaise(true);
    homeButton_->setFocusPolicy(Qt::NoFocus);
    homeButton_->setProperty("variant", "secondary");

    titleLabel_ = new QLabel("YouTube", topBar_);
    titleLabel_->setProperty("role", "subtitle");

    barLayout->addWidget(backButton_);
    barLayout->addWidget(homeButton_);
    barLayout->addSpacing(8);
    barLayout->addWidget(titleLabel_);
    barLayout->addStretch();

    view_ = new QWebEngineView(this);
    view_->setFocusPolicy(Qt::StrongFocus);

    root->addWidget(topBar_);
    root->addWidget(view_, 1);

    connect(backButton_, &QToolButton::clicked, this, [this]() { emit backRequested(); });
    connect(homeButton_, &QToolButton::clicked, this, [this]() { emit homeRequested(); });

    connect(view_->page(), &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest request) {
        request.accept();
        if (request.toggleOn()) {
            topBar_->hide();
        } else {
            topBar_->show();
        }
    });

    applyWebSettings();
}

void YouTubePlayerPage::applyWebSettings() {
    auto* profile = view_->page()->profile();
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile->setHttpUserAgent(
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36 AIOYouTubePlayer/1.0"
    );

    auto* settings = view_->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
}

void YouTubePlayerPage::setTopBarText(const QString& text) {
    titleLabel_->setText(text);
}

void YouTubePlayerPage::playVideoUrl(const QString& url) {
    setTopBarText("YouTube");
    view_->setUrl(QUrl(url));
    view_->setFocus();
}

void YouTubePlayerPage::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
        emit backRequested();
        event->accept();
        return;
    }

    if (view_) {
        QKeyEvent forwarded(event->type(), event->key(), event->modifiers(), event->text(), event->isAutoRepeat(), event->count());
        QCoreApplication::sendEvent(view_, &forwarded);
        if (forwarded.isAccepted()) {
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

} // namespace GUI
} // namespace AIO
