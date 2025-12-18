#include "gui/StreamingWebViewPage.h"

#include "gui/StreamingHubWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QToolButton>
#include <QVBoxLayout>

#include <QWebEngineView>
#include <QWebEngineHistory>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineCore/QWebEngineFullScreenRequest>

namespace AIO {
namespace GUI {

StreamingWebViewPage::StreamingWebViewPage(QWidget* parent)
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

    titleLabel_ = new QLabel("", topBar_);
    titleLabel_->setProperty("role", "subtitle");

    barLayout->addWidget(backButton_);
    barLayout->addWidget(homeButton_);
    barLayout->addSpacing(8);
    barLayout->addWidget(titleLabel_);
    barLayout->addStretch();

    view_ = new QWebEngineView(this);
    view_->setFocusPolicy(Qt::StrongFocus);

    // Avoid bright white flash before page styles apply.
    view_->page()->setBackgroundColor(QApplication::palette().color(QPalette::Window));

    root->addWidget(topBar_);
    root->addWidget(view_, 1);

    connect(backButton_, &QToolButton::clicked, this, [this]() {
        if (view_->history()->canGoBack()) view_->back();
    });
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

    // Help the view reliably receive key events.
    view_->installEventFilter(this);
    view_->page()->installEventFilter(this);
}

void StreamingWebViewPage::applyWebSettings() {
    auto* profile = view_->page()->profile();
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    profile->setHttpUserAgent(
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36 AIOStreaming/1.0"
    );

    auto* settings = view_->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
}

QString StreamingWebViewPage::titleForApp(AIO::GUI::StreamingApp app) const {
    switch (app) {
        case StreamingApp::YouTube: return "YouTube";
        case StreamingApp::Netflix: return "Netflix";
        case StreamingApp::DisneyPlus: return "Disney+";
        case StreamingApp::Hulu: return "Hulu";
    }
    return "Streaming";
}

QString StreamingWebViewPage::urlForApp(AIO::GUI::StreamingApp app) const {
    switch (app) {
        case StreamingApp::YouTube: return "about:blank";
        case StreamingApp::Netflix: return "https://www.netflix.com/browse";
        case StreamingApp::DisneyPlus: return "https://www.disneyplus.com/home";
        case StreamingApp::Hulu: return "https://www.hulu.com/hub/home";
    }
    return "https://www.youtube.com";
}

void StreamingWebViewPage::setTopBarText(const QString& text) {
    titleLabel_->setText(text);
}

void StreamingWebViewPage::openApp(AIO::GUI::StreamingApp app) {
    setTopBarText(titleForApp(app));
    view_->setUrl(QUrl(urlForApp(app)));
    view_->setFocus();
}

bool StreamingWebViewPage::eventFilter(QObject* watched, QEvent* event) {
    // Ensure the parent page-level shortcuts still work.
    if (event->type() == QEvent::FocusIn) {
        setFocus();
    }
    return QWidget::eventFilter(watched, event);
}

void StreamingWebViewPage::keyPressEvent(QKeyEvent* event) {
    // Global navigation keys for 10-foot UI.
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
        emit homeRequested();
        event->accept();
        return;
    }

    // Alt+Left like browsers.
    if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_Left) {
        if (view_->history()->canGoBack()) view_->back();
        event->accept();
        return;
    }

    // Otherwise let the webview handle typing/navigation.
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
