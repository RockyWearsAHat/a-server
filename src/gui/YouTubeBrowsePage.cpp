#include "gui/YouTubeBrowsePage.h"

#include "gui/ThumbnailCache.h"
#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <thread>

namespace AIO {
namespace GUI {

namespace {

constexpr auto kTileSelectedProperty = "aio_tile_selected";
constexpr auto kTileHoveredProperty = "aio_tile_hovered";
constexpr auto kRailIndexProperty = "aio_rail_index";
constexpr auto kItemIndexProperty = "aio_item_index";
constexpr auto kGuideIndexProperty = "aio_guide_index";
constexpr auto kAuthCardProperty = "aio_auth_card";

QString elideText(const QString &text, int maxLength) {
  QString normalized = text.simplified();
  if (normalized.size() <= maxLength) {
    return normalized;
  }
  return normalized.left(std::max(0, maxLength - 1)) + QChar(0x2026);
}

QString formatDuration(int seconds) {
  if (seconds <= 0) {
    return QString();
  }
  const int hours = seconds / 3600;
  const int minutes = (seconds % 3600) / 60;
  const int secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10,
                                                  QLatin1Char('0'));
}

int railIndexForObject(QObject *object) {
  if (!object) {
    return -1;
  }
  bool ok = false;
  const int value = object->property(kRailIndexProperty).toInt(&ok);
  return ok ? value : -1;
}

int itemIndexForObject(QObject *object) {
  if (!object) {
    return -1;
  }
  bool ok = false;
  const int value = object->property(kItemIndexProperty).toInt(&ok);
  return ok ? value : -1;
}

bool isAuthObject(QObject *object) {
  return object && object->property(kAuthCardProperty).toBool();
}

int guideIndexForObject(QObject *object) {
  if (!object) {
    return -1;
  }
  bool ok = false;
  const int value = object->property(kGuideIndexProperty).toInt(&ok);
  return ok ? value : -1;
}

QString qrImageUrl(const QString &target) {
  if (target.trimmed().isEmpty()) {
    return QString();
  }
  return QStringLiteral("https://api.qrserver.com/v1/create-qr-code/"
                        "?size=220x220&margin=0&data=%1")
      .arg(QString::fromUtf8(QUrl::toPercentEncoding(target)));
}

QPixmap circularPixmap(const QPixmap &source, int diameter) {
  if (source.isNull() || diameter <= 0) {
    return QPixmap();
  }

  QPixmap scaled =
      source.scaled(diameter, diameter, Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
  QPixmap result(diameter, diameter);
  result.fill(Qt::transparent);

  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath path;
  path.addEllipse(0, 0, diameter, diameter);
  painter.setClipPath(path);
  painter.drawPixmap(0, 0, scaled);
  return result;
}

QPixmap guideIconPixmap(const QString &key, const QSize &size,
                        const QColor &color) {
  QPixmap pixmap(size);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  const QRectF box(2.0, 2.0, size.width() - 4.0, size.height() - 4.0);

  if (key == QStringLiteral("home")) {
    QPainterPath path;
    path.moveTo(box.left() + 2.0, box.center().y() - 1.0);
    path.lineTo(box.center().x(), box.top() + 2.0);
    path.lineTo(box.right() - 2.0, box.center().y() - 1.0);
    path.lineTo(box.right() - 2.0, box.bottom() - 2.0);
    path.lineTo(box.left() + 2.0, box.bottom() - 2.0);
    path.closeSubpath();
    painter.drawPath(path);
    painter.drawLine(QPointF(box.center().x(), box.bottom() - 2.0),
                     QPointF(box.center().x(), box.center().y() + 2.0));
  } else if (key == QStringLiteral("search")) {
    painter.drawEllipse(QRectF(box.left() + 2.0, box.top() + 2.0, 10.0, 10.0));
    painter.drawLine(QPointF(box.center().x() + 2.0, box.center().y() + 2.0),
                     QPointF(box.right() - 1.0, box.bottom() - 1.0));
  } else if (key == QStringLiteral("continue") ||
             key == QStringLiteral("watch_later")) {
    painter.drawEllipse(box.adjusted(2.0, 2.0, -2.0, -2.0));
    painter.drawLine(QPointF(box.center().x(), box.center().y()),
                     QPointF(box.center().x(), box.top() + 5.0));
    painter.drawLine(QPointF(box.center().x(), box.center().y()),
                     QPointF(box.right() - 5.0, box.center().y()));
  } else if (key == QStringLiteral("subscriptions")) {
    painter.drawRoundedRect(box.adjusted(2.0, 4.0, -2.0, -4.0), 4.0, 4.0);
    QPainterPath play;
    play.moveTo(box.center().x() - 2.0, box.center().y() - 4.0);
    play.lineTo(box.center().x() + 4.0, box.center().y());
    play.lineTo(box.center().x() - 2.0, box.center().y() + 4.0);
    play.closeSubpath();
    painter.fillPath(play, color);
  } else if (key == QStringLiteral("liked")) {
    QPainterPath heart;
    heart.moveTo(box.center().x(), box.bottom() - 3.0);
    heart.cubicTo(box.right() - 1.0, box.center().y() + 2.0, box.right() - 1.0,
                  box.top() + 2.0, box.center().x(), box.top() + 5.0);
    heart.cubicTo(box.left() + 1.0, box.top() + 2.0, box.left() + 1.0,
                  box.center().y() + 2.0, box.center().x(), box.bottom() - 3.0);
    painter.fillPath(heart, color);
  } else if (key == QStringLiteral("account") ||
             key == QStringLiteral("connect")) {
    painter.drawEllipse(
        QRectF(box.center().x() - 4.0, box.top() + 2.0, 8.0, 8.0));
    painter.drawArc(QRectF(box.center().x() - 7.0, box.top() + 9.0, 14.0, 10.0),
                    0, 180 * 16);
  } else if (key == QStringLiteral("recommended")) {
    painter.drawEllipse(box.adjusted(2.0, 2.0, -2.0, -2.0));
    painter.drawLine(QPointF(box.center().x(), box.top() + 4.0),
                     QPointF(box.center().x(), box.bottom() - 4.0));
    painter.drawLine(QPointF(box.left() + 4.0, box.center().y()),
                     QPointF(box.right() - 4.0, box.center().y()));
  } else if (key == QStringLiteral("trending")) {
    painter.drawLine(QPointF(box.left() + 2.0, box.bottom() - 4.0),
                     QPointF(box.center().x() - 1.0, box.center().y() + 2.0));
    painter.drawLine(QPointF(box.center().x() - 1.0, box.center().y() + 2.0),
                     QPointF(box.right() - 3.0, box.top() + 4.0));
    painter.drawLine(QPointF(box.right() - 7.0, box.top() + 4.0),
                     QPointF(box.right() - 3.0, box.top() + 4.0));
    painter.drawLine(QPointF(box.right() - 3.0, box.top() + 4.0),
                     QPointF(box.right() - 3.0, box.top() + 8.0));
  } else if (key == QStringLiteral("gaming")) {
    painter.drawRoundedRect(box.adjusted(2.0, 5.0, -2.0, -3.0), 6.0, 6.0);
    painter.drawLine(QPointF(box.left() + 6.0, box.center().y()),
                     QPointF(box.left() + 11.0, box.center().y()));
    painter.drawLine(QPointF(box.left() + 8.5, box.center().y() - 2.5),
                     QPointF(box.left() + 8.5, box.center().y() + 2.5));
    painter.drawEllipse(
        QRectF(box.right() - 10.0, box.center().y() - 2.0, 2.5, 2.5));
    painter.drawEllipse(
        QRectF(box.right() - 6.0, box.center().y() + 0.5, 2.5, 2.5));
  } else if (key == QStringLiteral("music")) {
    painter.drawLine(QPointF(box.center().x() + 2.0, box.top() + 3.0),
                     QPointF(box.center().x() + 2.0, box.bottom() - 4.0));
    painter.drawLine(QPointF(box.center().x() + 2.0, box.top() + 3.0),
                     QPointF(box.right() - 2.0, box.top() + 5.0));
    painter.drawEllipse(
        QRectF(box.center().x() - 7.0, box.bottom() - 8.0, 5.0, 5.0));
    painter.drawEllipse(
        QRectF(box.right() - 7.0, box.bottom() - 6.0, 5.0, 5.0));
  } else {
    painter.drawEllipse(box.adjusted(2.0, 2.0, -2.0, -2.0));
  }

  return pixmap;
}

} // namespace

YouTubeBrowsePage::YouTubeBrowsePage(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);

  auto service = AIO::Streaming::StreamingManager::getInstance().getService(
      AIO::Streaming::StreamingServiceType::YouTube);
  youTube_ = dynamic_cast<AIO::Streaming::YouTubeService *>(service.get());
  authSession_ = new AIO::Streaming::YouTubeDeviceAuthSession();

  setupUi();
  loadTrending();
}

YouTubeBrowsePage::~YouTubeBrowsePage() {
  delete authSession_;
  authSession_ = nullptr;
}

void YouTubeBrowsePage::setupUi() {
  auto *root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  sidebar_ = new QFrame(this);
  sidebar_->setObjectName("aioYouTubeSidebar");
  sidebar_->setMinimumWidth(88);
  sidebar_->setMaximumWidth(320);
  sidebar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
  auto *sidebarLayout = new QVBoxLayout(sidebar_);
  sidebarLayout->setContentsMargins(14, 18, 14, 18);
  sidebarLayout->setSpacing(14);

  sidebarTitleLabel_ = new QLabel("YT", sidebar_);
  sidebarTitleLabel_->setObjectName("aioYouTubeSidebarBrand");
  sidebarTitleLabel_->setAlignment(Qt::AlignCenter);
  sidebarTitleLabel_->setFixedSize(56, 56);
  sidebarLayout->addWidget(sidebarTitleLabel_, 0, Qt::AlignHCenter);

  guideList_ = new QWidget(sidebar_);
  guideLayout_ = new QVBoxLayout(guideList_);
  guideLayout_->setContentsMargins(0, 0, 0, 0);
  guideLayout_->setSpacing(10);
  sidebarLayout->addWidget(guideList_);
  sidebar_->installEventFilter(this);
  guideList_->installEventFilter(this);
  sidebarLayout->addStretch();

  auto *mainShell = new QWidget(this);
  mainShell->setObjectName("aioYouTubeMainShell");
  auto *mainLayout = new QVBoxLayout(mainShell);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  topBar_ = new QWidget(mainShell);
  topBar_->setObjectName("aioTopBar");
  topBar_->installEventFilter(this);
  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(20, 14, 20, 14);
  barLayout->setSpacing(12);

  backButton_ = new QPushButton("Back", topBar_);
  backButton_->setFocusPolicy(Qt::NoFocus);
  backButton_->setProperty("variant", "secondary");

  homeButton_ = new QPushButton("Home", topBar_);
  homeButton_->setFocusPolicy(Qt::NoFocus);
  homeButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("YouTube", topBar_);
  titleLabel_->setVisible(false);

  searchEdit_ = new QLineEdit(topBar_);
  searchEdit_->setPlaceholderText("Search YouTube");
  searchEdit_->setObjectName("aioYouTubeSearch");

  searchButton_ = new QPushButton("Search", topBar_);
  searchButton_->setFocusPolicy(Qt::NoFocus);
  searchButton_->setProperty("variant", "secondary");
  searchButton_->setObjectName("aioYouTubeSearchButton");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(searchEdit_, 1);
  barLayout->addWidget(searchButton_);

  authCard_ = new QFrame(mainShell);
  authCard_->setObjectName("aioYouTubeAccountStrip");
  authCard_->setProperty(kAuthCardProperty, true);
  authCard_->setFocusPolicy(Qt::NoFocus);
  authCard_->installEventFilter(this);

  auto *authLayout = new QVBoxLayout(authCard_);
  authLayout->setContentsMargins(20, 12, 20, 12);
  authLayout->setSpacing(8);

  auto *authTopRow = new QHBoxLayout();
  authTopRow->setSpacing(16);

  auto *authCopyColumn = new QVBoxLayout();
  authCopyColumn->setContentsMargins(0, 0, 0, 0);
  authCopyColumn->setSpacing(3);

  authAvatarLabel_ = new QLabel(authCard_);
  authAvatarLabel_->setObjectName("aioYouTubeAccountAvatar");
  authAvatarLabel_->setAlignment(Qt::AlignCenter);
  authAvatarLabel_->setFixedSize(48, 48);
  authAvatarLabel_->setText("YT");

  authTitleLabel_ = new QLabel(authCard_);
  authTitleLabel_->setProperty("role", "ytSectionTitle");
  authBodyLabel_ = new QLabel(authCard_);
  authBodyLabel_->setProperty("role", "ytSectionMeta");
  authBodyLabel_->setWordWrap(true);
  authCodeLabel_ = new QLabel(authCard_);
  authCodeLabel_->setProperty("role", "tileTitle");
  authUrlLabel_ = new QLabel(authCard_);
  authUrlLabel_->setProperty("role", "tileMeta");
  authHintLabel_ = new QLabel(authCard_);
  authHintLabel_->setProperty("role", "tileMeta");
  authHintLabel_->setWordWrap(true);
  statusLabel_ = new QLabel(authCard_);
  statusLabel_->setObjectName("aioYouTubeStatusLabel");
  statusLabel_->setWordWrap(true);
  statusLabel_->hide();
  authFooterLabel_ = new QLabel(authCard_);
  authFooterLabel_->setProperty("role", "tileMeta");
  authFooterLabel_->setWordWrap(true);
  authQrLabel_ = new QLabel(authCard_);
  authQrLabel_->setObjectName("aioYouTubeAuthQr");
  authQrLabel_->setAlignment(Qt::AlignCenter);
  authQrLabel_->setFixedSize(120, 120);
  authQrLabel_->setText("Scan");

  auto *authButtons = new QHBoxLayout();
  authButtons->setSpacing(8);
  authPrimaryButton_ = new QPushButton(authCard_);
  authPrimaryButton_->setFocusPolicy(Qt::NoFocus);
  authSecondaryButton_ = new QPushButton(authCard_);
  authSecondaryButton_->setFocusPolicy(Qt::NoFocus);
  authSecondaryButton_->setProperty("variant", "secondary");
  authButtons->addWidget(authPrimaryButton_);
  authButtons->addWidget(authSecondaryButton_);
  authButtons->addStretch();

  auto *identityRow = new QHBoxLayout();
  identityRow->setContentsMargins(0, 0, 0, 0);
  identityRow->setSpacing(12);
  identityRow->addWidget(authAvatarLabel_, 0, Qt::AlignTop);

  auto *identityText = new QVBoxLayout();
  identityText->setContentsMargins(0, 0, 0, 0);
  identityText->setSpacing(3);
  identityText->addWidget(authTitleLabel_);
  identityText->addWidget(authBodyLabel_);
  identityText->addWidget(authHintLabel_);
  identityText->addWidget(statusLabel_);
  identityText->addWidget(authFooterLabel_);
  identityRow->addLayout(identityText, 1);

  authCopyColumn->addLayout(identityRow);
  authCopyColumn->addWidget(authCodeLabel_);
  authCopyColumn->addWidget(authUrlLabel_);

  authTopRow->addLayout(authCopyColumn, 1);
  authTopRow->addWidget(authQrLabel_, 0, Qt::AlignTop | Qt::AlignRight);

  authLayout->addLayout(authTopRow);
  authLayout->addLayout(authButtons);

  auto *contentWrapper = new QWidget(mainShell);
  auto *contentLayout = new QVBoxLayout(contentWrapper);
  contentLayout->setContentsMargins(24, 14, 28, 20);
  contentLayout->setSpacing(10);

  scroll_ = new QScrollArea(contentWrapper);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->installEventFilter(this);

  contentHost_ = new QWidget(scroll_);
  contentLayout_ = new QVBoxLayout(contentHost_);
  contentLayout_->setContentsMargins(0, 0, 0, 0);
  contentLayout_->setSpacing(18);

  scroll_->setWidget(contentHost_);
  contentLayout->addWidget(scroll_, 1);

  mainLayout->addWidget(topBar_);
  mainLayout->addWidget(authCard_);
  mainLayout->addWidget(contentWrapper, 1);

  root->addWidget(sidebar_);
  root->addWidget(mainShell, 1);

  authPollTimer_ = new QTimer(this);
  authPollTimer_->setSingleShot(false);

  connect(backButton_, &QPushButton::clicked, this,
          [this]() { emit homeRequested(); });
  connect(homeButton_, &QPushButton::clicked, this,
          [this]() { emit homeRequested(); });
  connect(searchButton_, &QPushButton::clicked, this,
          &YouTubeBrowsePage::runSearch);
  connect(searchEdit_, &QLineEdit::returnPressed, this,
          &YouTubeBrowsePage::runSearch);
  connect(authPrimaryButton_, &QPushButton::clicked, this,
          &YouTubeBrowsePage::performAuthPrimaryAction);
  connect(authSecondaryButton_, &QPushButton::clicked, this, [this]() {
    if (authSession_ && authSession_->active) {
      if (youTube_) {
        youTube_->cancelDeviceAuth();
      }
      authPollTimer_->stop();
      *authSession_ = AIO::Streaming::YouTubeDeviceAuthSession{};
      rebuildAuthCard();
      setStatus("YouTube sign-in cancelled.");
    } else if (youTube_ && youTube_->hasOAuthAccess()) {
      signOutYouTube();
    }
  });
  connect(authPollTimer_, &QTimer::timeout, this,
          &YouTubeBrowsePage::pollDeviceAuth);
  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &) {
            updateFocusStyle();
            rebuildAuthCard();
          });

  rebuildGuide();
  rebuildAuthCard();
  updateSectionHeader();
  updateHeroSpotlight();
  updateSidebarState(false);
}

void YouTubeBrowsePage::setStatus(const QString &text) {
  statusLabel_->setText(text);
  const bool hasText = !text.trimmed().isEmpty();
  statusLabel_->setVisible(hasText);
}

void YouTubeBrowsePage::setLoadingState(bool loading, const QString &text) {
  setStatus(text);
  searchButton_->setEnabled(!loading);
  authPrimaryButton_->setEnabled(!loading);
}

QString
YouTubeBrowsePage::summaryFor(const AIO::Streaming::VideoContent &item) const {
  QString description = QString::fromStdString(item.description);
  description.replace(QRegularExpression(QStringLiteral("https?://\\S+")),
                      QString());
  description.replace(QRegularExpression(QStringLiteral("(?i)download:?\\s*")),
                      QString());
  description.replace(QRegularExpression(QStringLiteral("(?i)stream/")),
                      QString());
  description = description.simplified();
  if (!description.isEmpty()) {
    return elideText(description, 78);
  }
  return QStringLiteral("Ready to play");
}

void YouTubeBrowsePage::loadTrending() { refreshHome(); }

void YouTubeBrowsePage::refreshHome() {
  if (!youTube_) {
    setStatus("YouTube service not available.");
    return;
  }

  setLoadingState(true, "Loading YouTube home...");
  const uint64_t requestId = ++requestSerial_;
  const int railSize = itemsPerRail();
  QPointer<YouTubeBrowsePage> guard(this);
  auto *service = youTube_;

  std::thread([guard, requestId, railSize, service]() {
    const auto rails = service
                           ? service->getHomeRails(railSize)
                           : std::vector<AIO::Streaming::YouTubeContentRail>{};
    const bool signedIn = service && service->hasOAuthAccess();
    const QString accountLabel =
        service ? QString::fromStdString(service->getAccountDisplayName())
                : QString();
    const QString heroBody =
        signedIn
            ? QStringLiteral("Pick up where you left off, jump into "
                             "subscription uploads, or move straight into "
                             "recommendation rails that reflect this account.")
            : QStringLiteral(
                  "Connect a YouTube account for continue watching, watch "
                  "later, liked videos, and subscription rails. While signed "
                  "out, this page falls back to public picks.");

    QMetaObject::invokeMethod(
        guard.data(),
        [guard, requestId, rails, heroBody, accountLabel]() {
          if (!guard || requestId != guard->requestSerial_) {
            return;
          }
          guard->setRails(rails, heroBody, accountLabel);
          guard->setLoadingState(
              false, rails.empty()
                         ? QStringLiteral("No videos available right now.")
                         : QStringLiteral("%1 rows ready").arg(rails.size()));
        },
        Qt::QueuedConnection);
  }).detach();
}

void YouTubeBrowsePage::runSearch() {
  if (!youTube_) {
    setStatus("YouTube service not available.");
    return;
  }

  const QString query = searchEdit_->text().trimmed();
  if (query.isEmpty()) {
    refreshHome();
    return;
  }

  setLoadingState(true, QStringLiteral("Searching for \"%1\"...").arg(query));
  const uint64_t requestId = ++requestSerial_;
  QPointer<YouTubeBrowsePage> guard(this);
  auto *service = youTube_;
  const std::string searchQuery = query.toStdString();

  std::thread([guard, requestId, service, searchQuery]() {
    std::vector<AIO::Streaming::YouTubeContentRail> rails;
    if (service) {
      auto results = service->search(searchQuery, 18);
      rails.push_back({
          "search",
          "Search results",
          "Open a result directly or back out to return to the account home.",
          std::move(results),
      });
    }
    const QString accountLabel =
        service ? QString::fromStdString(service->getAccountDisplayName())
                : QString();

    QMetaObject::invokeMethod(
        guard.data(),
        [guard, requestId, rails, query = QString::fromStdString(searchQuery),
         accountLabel]() {
          if (!guard || requestId != guard->requestSerial_) {
            return;
          }
          guard->setRails(
              rails, QStringLiteral("Search results for \"%1\".").arg(query),
              accountLabel);
          const int resultCount =
              rails.empty() ? 0 : static_cast<int>(rails.front().items.size());
          guard->setLoadingState(
              false,
              resultCount > 0
                  ? QStringLiteral("%1 results ready").arg(resultCount)
                  : QStringLiteral("No results found for \"%1\".").arg(query));
        },
        Qt::QueuedConnection);
  }).detach();
}

void YouTubeBrowsePage::setRails(
    const std::vector<AIO::Streaming::YouTubeContentRail> &rails,
    const QString &heroBody, const QString &accountLabel) {
  rails_.clear();
  rails_.reserve(rails.size());
  for (const auto &rail : rails) {
    RailModel model;
    model.key = QString::fromStdString(rail.key);
    model.title = QString::fromStdString(rail.title);
    model.subtitle = QString::fromStdString(rail.subtitle);
    model.items = rail.items;
    rails_.push_back(std::move(model));
  }

  accountLabel_ = accountLabel;
  accountAvatarUrl_ =
      youTube_ ? QString::fromStdString(youTube_->getAccountAvatarUrl())
               : QString();
  Q_UNUSED(heroBody);
  currentSectionTitle_ =
      rails_.empty() ? QStringLiteral("For you") : rails_.front().title;

  rebuildGuide();
  railSelections_.assign(rails_.size(), 0);
  focusedRailIndex_ = rails_.empty() ? -1 : 0;
  focusedItemIndex_ = 0;
  guideSelected_ = true;
  authCardSelected_ = false;
  clearHover();
  rebuildAuthCard();
  rebuildContent();

  updateHeroSpotlight();
}

void YouTubeBrowsePage::rebuildGuide() {
  if (!guideLayout_) {
    return;
  }

  while (QLayoutItem *item = guideLayout_->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  guideItems_.clear();
  guideButtons_.clear();
  libraryHeader_ = nullptr;

  const bool signedIn = youTube_ && youTube_->hasOAuthAccess();
  guideItems_.push_back(
      {QStringLiteral("home"), QStringLiteral("Home"), QString(), false});
  guideItems_.push_back(
      {QStringLiteral("search"), QStringLiteral("Search"), QString(), false});

  if (signedIn) {
    libraryHeader_ = new QFrame(guideList_);
    libraryHeader_->setObjectName("aioYouTubeGuideSection");
    libraryHeader_->setProperty("aio_expanded", librarySectionExpanded_);
    libraryHeader_->installEventFilter(this);

    auto *headerLayout = new QHBoxLayout(libraryHeader_);
    headerLayout->setContentsMargins(8, 6, 8, 6);
    headerLayout->setSpacing(10);

    auto *chevron = new QLabel(libraryHeader_);
    chevron->setObjectName("aioYouTubeGuideSectionChevron");
    chevron->setText(librarySectionExpanded_ ? QStringLiteral("v")
                                             : QStringLiteral(">"));
    chevron->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *label = new QLabel(QStringLiteral("You"), libraryHeader_);
    label->setObjectName("aioYouTubeGuideSectionLabel");
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    headerLayout->addWidget(chevron);
    headerLayout->addWidget(label);
    headerLayout->addStretch();
    guideLayout_->addWidget(libraryHeader_);
  }

  if (signedIn) {
    guideItems_.push_back({QStringLiteral("continue"),
                           QStringLiteral("Continue"), QString(), true});
    guideItems_.push_back({QStringLiteral("subscriptions"),
                           QStringLiteral("Subscriptions"), QString(), true});
    guideItems_.push_back({QStringLiteral("watch_later"),
                           QStringLiteral("Watch later"), QString(), true});
    guideItems_.push_back(
        {QStringLiteral("liked"), QStringLiteral("Liked"), QString(), true});
    guideItems_.push_back({QStringLiteral("account"), QStringLiteral("Account"),
                           QString(), false});
  } else {
    guideItems_.push_back({QStringLiteral("connect"), QStringLiteral("Sign in"),
                           QString(), false});
    guideItems_.push_back({QStringLiteral("recommended"),
                           QStringLiteral("Browse"), QString(), false});
    guideItems_.push_back({QStringLiteral("trending"),
                           QStringLiteral("Trending"), QString(), false});
    guideItems_.push_back(
        {QStringLiteral("gaming"), QStringLiteral("Gaming"), QString(), false});
    guideItems_.push_back(
        {QStringLiteral("music"), QStringLiteral("Music"), QString(), false});
  }

  selectedGuideIndex_ =
      std::clamp(selectedGuideIndex_, 0,
                 std::max(0, static_cast<int>(guideItems_.size()) - 1));

  for (int index = 0; index < static_cast<int>(guideItems_.size()); ++index) {
    if (guideItems_[index].inLibrarySection && !librarySectionExpanded_) {
      continue;
    }

    auto *button = new QFrame(guideList_);
    button->setObjectName("aioYouTubeGuideItem");
    button->setProperty(kGuideIndexProperty, index);
    button->setProperty("aio_selected", false);
    button->setProperty("aio_hovered", false);
    button->setFocusPolicy(Qt::NoFocus);
    button->installEventFilter(this);

    auto *buttonLayout = new QHBoxLayout(button);
    buttonLayout->setContentsMargins(8, 7, 8, 7);
    buttonLayout->setSpacing(10);

    auto *iconLabel = new QLabel(button);
    iconLabel->setObjectName("aioYouTubeGuideIconWrap");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(42, 42);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *textClip = new QWidget(button);
    textClip->setObjectName("guideTextClip");
    textClip->setMaximumWidth(guideSelected_ ? 134 : 0);
    textClip->setMinimumWidth(0);
    textClip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    textClip->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *textClipLayout = new QHBoxLayout(textClip);
    textClipLayout->setContentsMargins(0, 0, 0, 0);
    textClipLayout->setSpacing(0);

    auto *textLabel = new QLabel(guideItems_[index].title, textClip);
    textLabel->setObjectName("guideText");
    textLabel->setProperty("role", "guideText");
    textLabel->setMinimumWidth(134);
    textLabel->setMaximumWidth(134);
    textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *opacityEffect = new QGraphicsOpacityEffect(textLabel);
    opacityEffect->setOpacity(guideSelected_ ? 1.0 : 0.0);
    textLabel->setGraphicsEffect(opacityEffect);
    textClipLayout->addWidget(textLabel);

    buttonLayout->addWidget(iconLabel);
    buttonLayout->addWidget(textClip);
    buttonLayout->addStretch();

    guideLayout_->addWidget(button);
    guideButtons_.push_back(button);
  }

  guideLayout_->addStretch();
  updateSidebarState(false);
}

void YouTubeBrowsePage::toggleLibrarySection() {
  librarySectionExpanded_ = !librarySectionExpanded_;
  if (!librarySectionExpanded_ && selectedGuideIndex_ >= 0 &&
      selectedGuideIndex_ < static_cast<int>(guideItems_.size()) &&
      guideItems_[selectedGuideIndex_].inLibrarySection) {
    selectedGuideIndex_ = 0;
  }
  rebuildGuide();
  updateFocusStyle();
}

void YouTubeBrowsePage::rebuildAuthCard() {
  if (!youTube_ || !authSession_) {
    authCard_->hide();
    return;
  }

  const bool signedIn = youTube_->hasOAuthAccess();
  const bool active = authSession_->active;
  const bool canStartDeviceAuth = youTube_->hasDeviceAuthClient();

  authCard_->show();
  authCard_->setProperty(kTileSelectedProperty, false);
  authCodeLabel_->setVisible(false);
  authUrlLabel_->setVisible(false);
  authSecondaryButton_->setVisible(false);
  authQrLabel_->setVisible(false);
  authQrLabel_->setPixmap(QPixmap());
  authQrLabel_->setText("Scan");
  authAvatarLabel_->setPixmap(QPixmap());
  authAvatarLabel_->setText(accountLabel_.isEmpty()
                                ? QStringLiteral("YT")
                                : accountLabel_.left(1).toUpper());

  if (!accountAvatarUrl_.isEmpty()) {
    QPixmap avatarPixmap;
    if (ThumbnailCache::instance().tryGet(accountAvatarUrl_, &avatarPixmap)) {
      authAvatarLabel_->setPixmap(circularPixmap(avatarPixmap, 48));
      authAvatarLabel_->setText(QString());
    } else {
      ThumbnailCache::instance().request(accountAvatarUrl_);
    }
  }

  const QString qrUrl = qrImageUrlForSession();
  if (!qrUrl.isEmpty()) {
    QPixmap qrPixmap;
    if (ThumbnailCache::instance().tryGet(qrUrl, &qrPixmap)) {
      authQrLabel_->setPixmap(qrPixmap.scaled(
          authQrLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
      authQrLabel_->setText(QString());
    } else {
      ThumbnailCache::instance().request(qrUrl);
    }
  }

  if (active) {
    authTitleLabel_->setText("Finish sign-in on your phone");
    authBodyLabel_->setText(
        "Scan the QR code or open the verification page. This screen refreshes "
        "as soon as approval finishes.");
    authCodeLabel_->setText(
        authSession_->userCode.empty()
            ? QString()
            : QStringLiteral("Code: %1")
                  .arg(QString::fromStdString(authSession_->userCode)));
    authUrlLabel_->setText(
        authSession_->verificationUrl.empty()
            ? QString()
            : QStringLiteral("Open: %1")
                  .arg(QString::fromStdString(authSession_->verificationUrl)));
    authHintLabel_->setText(
        authSession_->statusMessage.empty()
            ? QStringLiteral("Waiting for approval...")
            : QString::fromStdString(authSession_->statusMessage));
    authFooterLabel_->setText(
        QStringLiteral("Code expires in about %1 seconds.")
            .arg(authSession_->secondsRemaining));
    authPrimaryButton_->setText("Open on Phone");
    authSecondaryButton_->setText("Cancel");
    authCodeLabel_->setVisible(true);
    authUrlLabel_->setVisible(true);
    authSecondaryButton_->setVisible(true);
    authQrLabel_->setVisible(!qrUrl.isEmpty());
  } else if (signedIn) {
    const QString accountName = accountLabel_.isEmpty()
                                    ? QStringLiteral("your account")
                                    : accountLabel_;
    authTitleLabel_->setText(
        QStringLiteral("Signed in as %1").arg(accountName));
    authBodyLabel_->setText("Subscriptions, history, saved videos, and "
                            "personalized rows are active.");
    authHintLabel_->setText(QStringLiteral("%1 rows ready").arg(rails_.size()));
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Refresh");
    authSecondaryButton_->setText("Sign out");
    authSecondaryButton_->setVisible(true);
    authQrLabel_->setVisible(false);
    authCodeLabel_->setVisible(false);
    authUrlLabel_->setVisible(false);
  } else if (!canStartDeviceAuth) {
    authTitleLabel_->setText("Sign-in server unavailable");
    authBodyLabel_->setText("Start the Node auth server to enable QR sign-in "
                            "and your personalized library.");
    authHintLabel_->setText(
        "Set AIO_YOUTUBE_SERVER_URL, or enable AIO_YOUTUBE_SERVER_AUTOBOOT=1. "
        "The server owns YOUTUBE_OAUTH_CLIENT_ID, "
        "YOUTUBE_OAUTH_CLIENT_SECRET, and YOUTUBE_API_KEY in server/.env.");
    authFooterLabel_->setText("Public browsing still works while signed out.");
    authPrimaryButton_->setText("Refresh");
  } else {
    authTitleLabel_->setText("Sign in for your library");
    authBodyLabel_->setText(
        "Connect a YouTube account to unlock subscriptions, watch later, "
        "likes, and watch progress.");
    authHintLabel_->setText(
        "Use the action here or open the icon rail and choose Sign in.");
    authFooterLabel_->setText("The signed-out browser is public only.");
    authPrimaryButton_->setText("Sign in");
  }

  updateFocusStyle();
}

void YouTubeBrowsePage::updateHeroSpotlight() {
  if (titleLabel_) {
    titleLabel_->setText(QStringLiteral("YouTube"));
  }
}

void YouTubeBrowsePage::updateSidebarState(bool animated) {
  if (!sidebar_) {
    return;
  }

  const bool shouldExpand = guideSelected_;
  sidebarExpanded_ = shouldExpand;

  for (int index = 0; index < static_cast<int>(guideButtons_.size()); ++index) {
    auto *button = guideButtons_[index];
    if (!button) {
      continue;
    }
    auto *textClip = button->findChild<QWidget *>("guideTextClip");
    auto *textLabel = button->findChild<QLabel *>("guideText");
    if (!textClip || !textLabel) {
      continue;
    }

    auto *effect =
        qobject_cast<QGraphicsOpacityEffect *>(textLabel->graphicsEffect());
    if (!effect) {
      effect = new QGraphicsOpacityEffect(textLabel);
      textLabel->setGraphicsEffect(effect);
    }

    const int targetTextWidth = shouldExpand ? 134 : 0;
    const qreal targetOpacity = shouldExpand ? 1.0 : 0.0;
    const int staggerDelay = index * 26;
    if (animated) {
      QTimer::singleShot(
          staggerDelay, this,
          [this, textClip, textLabel, effect, targetTextWidth,
           targetOpacity]() {
            auto *widthAnim =
                new QPropertyAnimation(textClip, "maximumWidth", textClip);
            widthAnim->setDuration(190);
            widthAnim->setStartValue(textClip->maximumWidth());
            widthAnim->setEndValue(targetTextWidth);
            widthAnim->setEasingCurve(QEasingCurve::OutCubic);
            QObject::connect(widthAnim, &QVariantAnimation::valueChanged, this,
                             [this]() {
                               if (sidebar_) {
                                 sidebar_->updateGeometry();
                               }
                               layout()->activate();
                             });
            widthAnim->start(QAbstractAnimation::DeleteWhenStopped);

            auto *opacityAnim =
                new QPropertyAnimation(effect, "opacity", effect);
            opacityAnim->setDuration(150);
            opacityAnim->setStartValue(effect->opacity());
            opacityAnim->setEndValue(targetOpacity);
            opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
            opacityAnim->start(QAbstractAnimation::DeleteWhenStopped);
          });
    } else {
      textClip->setMaximumWidth(targetTextWidth);
      effect->setOpacity(targetOpacity);
    }
  }

  sidebar_->updateGeometry();
  layout()->activate();
}

void YouTubeBrowsePage::rebuildContent() {
  while (QLayoutItem *item = contentLayout_->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  railScrolls_.clear();
  railTiles_.clear();

  for (int railIndex = 0; railIndex < static_cast<int>(rails_.size());
       ++railIndex) {
    const auto &rail = rails_[railIndex];

    auto *railBlock = new QWidget(contentHost_);
    auto *railLayout = new QVBoxLayout(railBlock);
    railLayout->setContentsMargins(0, 0, 0, 0);
    railLayout->setSpacing(6);

    auto *title = new QLabel(rail.title, railBlock);
    title->setProperty("role", "ytSectionTitle");
    auto *subtitle = new QLabel(rail.subtitle, railBlock);
    subtitle->setProperty("role", "ytSectionMeta");
    subtitle->setWordWrap(true);
    subtitle->setVisible(!rail.subtitle.trimmed().isEmpty());

    auto *railScroll = new QScrollArea(railBlock);
    railScroll->setWidgetResizable(true);
    railScroll->setFrameShape(QFrame::NoFrame);
    railScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setFocusPolicy(Qt::NoFocus);

    auto *railHost = new QWidget(railScroll);
    auto *itemsLayout = new QHBoxLayout(railHost);
    itemsLayout->setContentsMargins(0, 0, 0, 0);
    itemsLayout->setSpacing(12);

    std::vector<QFrame *> tiles;
    tiles.reserve(rail.items.size());

    for (int itemIndex = 0; itemIndex < static_cast<int>(rail.items.size());
         ++itemIndex) {
      const auto &item = rail.items[itemIndex];
      auto *tile = new QFrame(railHost);
      tile->setObjectName("aioTile");
      tile->setFixedSize(304, 224);
      tile->setProperty(kRailIndexProperty, railIndex);
      tile->setProperty(kItemIndexProperty, itemIndex);
      tile->setProperty(kTileSelectedProperty, false);
      tile->setProperty(kTileHoveredProperty, false);
      tile->setFocusPolicy(Qt::NoFocus);
      tile->installEventFilter(this);

      auto *tileLayout = new QVBoxLayout(tile);
      tileLayout->setContentsMargins(0, 0, 0, 0);
      tileLayout->setSpacing(8);

      auto *thumb = new QLabel(tile);
      thumb->setObjectName("thumb");
      thumb->setProperty("role", "thumb");
      thumb->setAlignment(Qt::AlignCenter);
      thumb->setFixedSize(304, 172);
      thumb->setText("Loading");
      thumb->setProperty(kRailIndexProperty, railIndex);
      thumb->setProperty(kItemIndexProperty, itemIndex);
      thumb->installEventFilter(this);

      auto *copyWrap = new QWidget(tile);
      auto *copyLayout = new QVBoxLayout(copyWrap);
      copyLayout->setContentsMargins(10, 0, 10, 10);
      copyLayout->setSpacing(6);

      auto *itemTitle = new QLabel(QString::fromStdString(item.title), tile);
      itemTitle->setProperty("role", "tileTitle");
      itemTitle->setWordWrap(true);
      itemTitle->setProperty(kRailIndexProperty, railIndex);
      itemTitle->setProperty(kItemIndexProperty, itemIndex);
      itemTitle->installEventFilter(this);

      const QString categoryText =
          QString::fromStdString(item.category).trimmed();
      const QString durationText = formatDuration(item.durationSeconds);

      auto *chipRow = new QWidget(tile);
      auto *chipLayout = new QHBoxLayout(chipRow);
      chipLayout->setContentsMargins(0, 0, 0, 0);
      chipLayout->setSpacing(6);

      auto *categoryChip = new QLabel(chipRow);
      categoryChip->setProperty("role", "tileBadge");
      categoryChip->setText(categoryText);
      categoryChip->setVisible(!categoryText.isEmpty());

      auto *durationChip = new QLabel(chipRow);
      durationChip->setProperty("role", "tileBadge");
      durationChip->setText(durationText);
      durationChip->setVisible(!durationText.isEmpty());

      chipLayout->addWidget(categoryChip);
      chipLayout->addWidget(durationChip);
      chipLayout->addStretch();

      const bool hasBadges = !categoryText.isEmpty() || !durationText.isEmpty();

      auto *meta = new QLabel(summaryFor(item), tile);
      meta->setProperty("role", "tileMeta");
      meta->setWordWrap(true);
      meta->setVisible(!meta->text().trimmed().isEmpty());
      meta->setProperty(kRailIndexProperty, railIndex);
      meta->setProperty(kItemIndexProperty, itemIndex);
      meta->installEventFilter(this);

      const QString thumbUrl = QString::fromStdString(item.thumbnailUrl);
      if (!thumbUrl.isEmpty()) {
        QPixmap pixmap;
        if (ThumbnailCache::instance().tryGet(thumbUrl, &pixmap)) {
          thumb->setPixmap(pixmap.scaled(thumb->size(),
                                         Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation));
          thumb->setText(QString());
        } else {
          ThumbnailCache::instance().request(thumbUrl);
        }
      }

      tileLayout->addWidget(thumb);
      copyLayout->addWidget(itemTitle);
      if (hasBadges) {
        copyLayout->addWidget(chipRow);
      }
      copyLayout->addWidget(meta);
      copyLayout->addStretch();
      tileLayout->addWidget(copyWrap);

      itemsLayout->addWidget(tile);
      tiles.push_back(tile);
    }

    itemsLayout->addStretch();
    railHost->setLayout(itemsLayout);
    railScroll->setWidget(railHost);

    railLayout->addWidget(title);
    railLayout->addWidget(subtitle);
    railLayout->addWidget(railScroll);

    contentLayout_->addWidget(railBlock);
    railScrolls_.push_back(railScroll);
    railTiles_.push_back(std::move(tiles));
  }

  contentLayout_->addStretch();
  if (scroll_) {
    scroll_->verticalScrollBar()->setValue(0);
  }
  updateSectionHeader();
  updateFocusStyle();
  updateHeroSpotlight();
}

void YouTubeBrowsePage::setInputMode(InputMode mode) {
  if (inputMode_ == mode) {
    return;
  }
  inputMode_ = mode;
  if (inputMode_ == InputMode::Nav) {
    if (!cursorHidden_) {
      QGuiApplication::setOverrideCursor(Qt::BlankCursor);
      cursorHidden_ = true;
    }
  } else if (cursorHidden_) {
    QGuiApplication::restoreOverrideCursor();
    cursorHidden_ = false;
  }
}

void YouTubeBrowsePage::clearHover() {
  hoveredGuideIndex_ = -1;
  authCardHovered_ = false;
  hoveredRailIndex_ = -1;
  hoveredItemIndex_ = -1;
  updateFocusStyle();
}

void YouTubeBrowsePage::setFocusedItem(int railIndex, int itemIndex,
                                       bool ensureVisible) {
  if (railIndex < 0 || railIndex >= static_cast<int>(rails_.size())) {
    return;
  }
  if (rails_[railIndex].items.empty()) {
    return;
  }

  const int clampedItem = std::clamp(
      itemIndex, 0, static_cast<int>(rails_[railIndex].items.size()) - 1);
  guideSelected_ = false;
  focusedRailIndex_ = railIndex;
  focusedItemIndex_ = clampedItem;
  authCardSelected_ = false;
  railSelections_[railIndex] = clampedItem;
  currentSectionTitle_ = rails_[railIndex].title;
  updateSidebarState(true);
  updateSectionHeader();
  updateFocusStyle();
  updateHeroSpotlight();
  if (ensureVisible) {
    ensureFocusedVisible();
  }
}

void YouTubeBrowsePage::setFocusToAuthCard(bool ensureVisible) {
  if (guideItems_.empty()) {
    return;
  }
  guideSelected_ = true;
  authCardSelected_ = false;
  for (int index = 0; index < static_cast<int>(guideItems_.size()); ++index) {
    if (guideItems_[index].key == QStringLiteral("account") ||
        guideItems_[index].key == QStringLiteral("connect")) {
      selectedGuideIndex_ = index;
      break;
    }
  }
  updateSidebarState(true);
  updateSectionHeader();
  updateFocusStyle();
  updateHeroSpotlight();
  if (ensureVisible) {
    ensureFocusedVisible();
  }
}

int YouTubeBrowsePage::effectiveItemIndexForRail(int railIndex) const {
  if (railIndex < 0 || railIndex >= static_cast<int>(rails_.size())) {
    return 0;
  }
  if (rails_[railIndex].items.empty()) {
    return 0;
  }
  if (railIndex >= static_cast<int>(railSelections_.size())) {
    return 0;
  }
  return std::clamp(railSelections_[railIndex], 0,
                    static_cast<int>(rails_[railIndex].items.size()) - 1);
}

int YouTubeBrowsePage::railIndexForGuideSelection() const {
  if (selectedGuideIndex_ < 0 ||
      selectedGuideIndex_ >= static_cast<int>(guideItems_.size())) {
    return rails_.empty() ? -1 : 0;
  }

  const QString key = guideItems_[selectedGuideIndex_].key;
  auto matchKey = [&](const QString &candidate) {
    for (int index = 0; index < static_cast<int>(rails_.size()); ++index) {
      if (rails_[index].key == candidate && !rails_[index].items.empty()) {
        return index;
      }
    }
    return -1;
  };

  int railIndex = matchKey(key);
  if (railIndex >= 0) {
    return railIndex;
  }
  if (key == QStringLiteral("home") || key == QStringLiteral("recommended") ||
      key == QStringLiteral("search")) {
    return rails_.empty() ? -1 : 0;
  }
  if (key == QStringLiteral("account") || key == QStringLiteral("connect")) {
    return rails_.empty() ? -1 : 0;
  }
  return rails_.empty() ? -1 : 0;
}

int YouTubeBrowsePage::itemsPerRail() const {
  const int viewportWidth = scroll_ && scroll_->viewport()
                                ? scroll_->viewport()->width()
                                : this->width();
  if (viewportWidth >= 2000) {
    return 7;
  }
  if (viewportWidth >= 1700) {
    return 6;
  }
  if (viewportWidth >= 1400) {
    return 5;
  }
  return 4;
}

bool YouTubeBrowsePage::hasInteractiveAuthCard() const { return false; }

void YouTubeBrowsePage::updateSectionHeader() {
  setWindowTitle(QStringLiteral("YouTube"));
}

QString YouTubeBrowsePage::qrImageUrlForSession() const {
  if (!authSession_) {
    return QString();
  }
  const QString completeUrl =
      QString::fromStdString(authSession_->verificationUrlComplete).trimmed();
  if (!completeUrl.isEmpty()) {
    return qrImageUrl(completeUrl);
  }
  const QString baseUrl =
      QString::fromStdString(authSession_->verificationUrl).trimmed();
  return qrImageUrl(baseUrl);
}

void YouTubeBrowsePage::updateFocusStyle() {
  if (libraryHeader_) {
    libraryHeader_->setProperty("aio_expanded", librarySectionExpanded_);
    libraryHeader_->style()->unpolish(libraryHeader_);
    libraryHeader_->style()->polish(libraryHeader_);
    libraryHeader_->update();
    if (auto *chevron = libraryHeader_->findChild<QLabel *>(
            "aioYouTubeGuideSectionChevron")) {
      chevron->setText(librarySectionExpanded_ ? QStringLiteral("v")
                                               : QStringLiteral(">"));
    }
  }

  for (int index = 0; index < static_cast<int>(guideButtons_.size()); ++index) {
    auto *button = guideButtons_[index];
    if (!button) {
      continue;
    }
    const int guideIndex = guideIndexForObject(button);
    const bool selected = guideSelected_ && guideIndex == selectedGuideIndex_;
    const bool hovered =
        inputMode_ == InputMode::Mouse && guideIndex == hoveredGuideIndex_;
    button->setProperty("aio_selected", selected);
    button->setProperty("aio_hovered", hovered);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();

    if (auto *iconLabel =
            button->findChild<QLabel *>("aioYouTubeGuideIconWrap")) {
      QColor iconColor = QColor(QStringLiteral("#f1f1f1"));
      if (selected) {
        iconColor = QColor(QStringLiteral("#111111"));
      } else if (hovered) {
        iconColor = QColor(QStringLiteral("#ffffff"));
      }
      const QString guideKey =
          guideIndex >= 0 && guideIndex < static_cast<int>(guideItems_.size())
              ? guideItems_[guideIndex].key
              : QString();
      iconLabel->setPixmap(guideIconPixmap(guideKey, QSize(18, 18), iconColor));
      iconLabel->setProperty("aio_selected", selected);
      iconLabel->setProperty("aio_hovered", hovered);
      iconLabel->style()->unpolish(iconLabel);
      iconLabel->style()->polish(iconLabel);
      iconLabel->update();
    }
  }

  if (authCard_) {
    authCard_->setProperty(kTileSelectedProperty, false);
    authCard_->setProperty(kTileHoveredProperty, false);
    authCard_->style()->unpolish(authCard_);
    authCard_->style()->polish(authCard_);
    authCard_->update();
  }

  for (int railIndex = 0; railIndex < static_cast<int>(railTiles_.size());
       ++railIndex) {
    for (int itemIndex = 0;
         itemIndex < static_cast<int>(railTiles_[railIndex].size());
         ++itemIndex) {
      auto *tile = railTiles_[railIndex][itemIndex];
      if (!tile) {
        continue;
      }
      const bool selected = !authCardSelected_ &&
                            railIndex == focusedRailIndex_ &&
                            itemIndex == focusedItemIndex_;
      const bool hovered = inputMode_ == InputMode::Mouse &&
                           railIndex == hoveredRailIndex_ &&
                           itemIndex == hoveredItemIndex_;
      tile->setProperty(kTileSelectedProperty, selected);
      tile->setProperty(kTileHoveredProperty, hovered);
      tile->style()->unpolish(tile);
      tile->style()->polish(tile);
      tile->update();

      const QString thumbUrl =
          rails_[railIndex].items[itemIndex].thumbnailUrl.empty()
              ? QString()
              : QString::fromStdString(
                    rails_[railIndex].items[itemIndex].thumbnailUrl);
      if (!thumbUrl.isEmpty()) {
        QPixmap pixmap;
        if (ThumbnailCache::instance().tryGet(thumbUrl, &pixmap)) {
          if (auto *thumb = tile->findChild<QLabel *>("thumb")) {
            thumb->setPixmap(pixmap.scaled(thumb->size(),
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation));
            thumb->setText(QString());
          }
        }
      }
    }
  }
}

void YouTubeBrowsePage::activateFocused() {
  if (guideSelected_) {
    activateGuideItem();
    return;
  }
  if (focusedRailIndex_ < 0 ||
      focusedRailIndex_ >= static_cast<int>(rails_.size())) {
    return;
  }
  if (focusedItemIndex_ < 0 ||
      focusedItemIndex_ >=
          static_cast<int>(rails_[focusedRailIndex_].items.size())) {
    return;
  }
  const QString url = QString::fromStdString(
      rails_[focusedRailIndex_].items[focusedItemIndex_].videoUrl);
  if (!url.isEmpty()) {
    emit videoRequested(url);
  }
}

void YouTubeBrowsePage::activateGuideItem() {
  if (selectedGuideIndex_ < 0 ||
      selectedGuideIndex_ >= static_cast<int>(guideItems_.size())) {
    return;
  }

  const QString key = guideItems_[selectedGuideIndex_].key;
  if (key == QStringLiteral("home")) {
    refreshHome();
    return;
  }
  if (key == QStringLiteral("search")) {
    setSearchFocused(true);
    return;
  }
  if (key == QStringLiteral("connect") || key == QStringLiteral("account")) {
    performAuthPrimaryAction();
    return;
  }

  const int railIndex = railIndexForGuideSelection();
  if (railIndex >= 0 && railIndex < static_cast<int>(rails_.size()) &&
      !rails_[railIndex].items.empty()) {
    setFocusedItem(railIndex, effectiveItemIndexForRail(railIndex), true);
  }
}

void YouTubeBrowsePage::ensureFocusedVisible() {
  if (guideSelected_) {
    return;
  }
  if (focusedRailIndex_ < 0 ||
      focusedRailIndex_ >= static_cast<int>(railTiles_.size())) {
    return;
  }
  if (focusedItemIndex_ < 0 ||
      focusedItemIndex_ >=
          static_cast<int>(railTiles_[focusedRailIndex_].size())) {
    return;
  }
  if (focusedRailIndex_ < static_cast<int>(railScrolls_.size())) {
    railScrolls_[focusedRailIndex_]->ensureWidgetVisible(
        railTiles_[focusedRailIndex_][focusedItemIndex_], 24, 24);
    scroll_->ensureWidgetVisible(railScrolls_[focusedRailIndex_], 24, 24);
  }
}

void YouTubeBrowsePage::moveFocus(int dx, int dy) {
  if (guideSelected_) {
    if (dy != 0 && !guideItems_.empty()) {
      selectedGuideIndex_ =
          std::clamp(selectedGuideIndex_ + dy, 0,
                     static_cast<int>(guideItems_.size()) - 1);
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
      return;
    }
    if (dx > 0) {
      const int railIndex = railIndexForGuideSelection();
      if (railIndex >= 0 && railIndex < static_cast<int>(rails_.size()) &&
          !rails_[railIndex].items.empty()) {
        setFocusedItem(railIndex, effectiveItemIndexForRail(railIndex), true);
      }
      return;
    }
    if (dx == 0 && dy == 0 && !rails_.empty() &&
        !rails_.front().items.empty()) {
      setFocusedItem(0, effectiveItemIndexForRail(0), true);
    }
    return;
  }

  if (focusedRailIndex_ < 0) {
    if (!guideItems_.empty()) {
      guideSelected_ = true;
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
    } else if (!rails_.empty() && !rails_.front().items.empty()) {
      setFocusedItem(0, effectiveItemIndexForRail(0), true);
    }
    return;
  }

  int railIndex = focusedRailIndex_;
  int itemIndex = focusedItemIndex_;

  if (dx != 0) {
    if (dx < 0 && itemIndex == 0) {
      guideSelected_ = true;
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
      return;
    }
    itemIndex += dx;
  }
  if (dy != 0) {
    railIndex += dy;
    if (railIndex < 0) {
      guideSelected_ = true;
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
      return;
    }
    railIndex = std::clamp(railIndex, 0, static_cast<int>(rails_.size()) - 1);
    itemIndex = effectiveItemIndexForRail(railIndex);
  }

  if (rails_[railIndex].items.empty()) {
    return;
  }
  setFocusedItem(railIndex, itemIndex, true);
}

void YouTubeBrowsePage::setSearchFocused(bool focused) {
  if (!searchEdit_) {
    return;
  }
  if (focused) {
    searchEdit_->setFocus();
    searchEdit_->selectAll();
  } else {
    setFocus();
  }
}

void YouTubeBrowsePage::startDeviceAuth() {
  if (!youTube_ || !authSession_) {
    return;
  }

  *authSession_ = youTube_->beginDeviceAuth();
  rebuildAuthCard();
  setStatus(QString::fromStdString(authSession_->statusMessage));

  if (authSession_->active) {
    authPollTimer_->start(std::max(1, authSession_->pollIntervalSeconds) *
                          1000);
    setFocusToAuthCard(true);
  } else {
    authPollTimer_->stop();
  }
}

void YouTubeBrowsePage::pollDeviceAuth() {
  if (!youTube_ || !authSession_) {
    authPollTimer_->stop();
    return;
  }

  *authSession_ = youTube_->pollDeviceAuth();
  rebuildAuthCard();
  setStatus(QString::fromStdString(authSession_->statusMessage));

  if (authSession_->active) {
    authPollTimer_->start(std::max(1, authSession_->pollIntervalSeconds) *
                          1000);
  } else {
    authPollTimer_->stop();
    if (authSession_->authenticated) {
      refreshHome();
    }
  }
}

void YouTubeBrowsePage::performAuthPrimaryAction() {
  if (!youTube_ || !authSession_) {
    return;
  }

  if (authSession_->active) {
    const QString openUrl =
        authSession_->verificationUrlComplete.empty()
            ? QString::fromStdString(authSession_->verificationUrl)
            : QString::fromStdString(authSession_->verificationUrlComplete);
    if (!openUrl.isEmpty()) {
      QDesktopServices::openUrl(QUrl(openUrl));
    }
    return;
  }

  if (youTube_->hasOAuthAccess()) {
    refreshHome();
    return;
  }

  startDeviceAuth();
}

void YouTubeBrowsePage::signOutYouTube() {
  if (!youTube_ || !authSession_) {
    return;
  }
  authPollTimer_->stop();
  youTube_->logout();
  *authSession_ = AIO::Streaming::YouTubeDeviceAuthSession{};
  refreshHome();
}

void YouTubeBrowsePage::keyPressEvent(QKeyEvent *event) {
  setInputMode(InputMode::Nav);
  clearHover();

  if (searchEdit_ && searchEdit_->hasFocus()) {
    if (event->key() == Qt::Key_Escape) {
      setSearchFocused(false);
      event->accept();
      return;
    }
    QWidget::keyPressEvent(event);
    return;
  }

  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
    emit homeRequested();
    event->accept();
    return;
  }

  if ((event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_F) ||
      event->key() == Qt::Key_Slash) {
    setSearchFocused(true);
    event->accept();
    return;
  }

  switch (event->key()) {
  case Qt::Key_Left:
    moveFocus(-1, 0);
    event->accept();
    return;
  case Qt::Key_Right:
    moveFocus(1, 0);
    event->accept();
    return;
  case Qt::Key_Up:
    moveFocus(0, -1);
    event->accept();
    return;
  case Qt::Key_Down:
    moveFocus(0, 1);
    event->accept();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    activateFocused();
    event->accept();
    return;
  default:
    break;
  }

  QWidget::keyPressEvent(event);
}

void YouTubeBrowsePage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  ensureFocusedVisible();
}

bool YouTubeBrowsePage::eventFilter(QObject *watched, QEvent *event) {
  if (scroll_ && watched == scroll_->viewport() &&
      event->type() == QEvent::KeyPress) {
    setFocus();
  }

  if ((watched == topBar_ || watched == authCard_ ||
       watched == scroll_->viewport()) &&
      event->type() == QEvent::Enter) {
    if (inputMode_ == InputMode::Mouse && focusedRailIndex_ >= 0 &&
        guideSelected_) {
      hoveredGuideIndex_ = -1;
      guideSelected_ = false;
      updateSidebarState(true);
      updateFocusStyle();
    }
  }

  if ((watched == sidebar_ || watched == guideList_) &&
      event->type() == QEvent::Leave) {
    hoveredGuideIndex_ = -1;
    if (inputMode_ == InputMode::Mouse && focusedRailIndex_ >= 0) {
      guideSelected_ = false;
      updateSidebarState(true);
    }
    updateFocusStyle();
  }

  if (watched == libraryHeader_) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        toggleLibrarySection();
        return true;
      }
    }
  }

  const int guideIndex = guideIndexForObject(watched);
  if (guideIndex >= 0) {
    if (event->type() == QEvent::Enter) {
      setInputMode(InputMode::Mouse);
      hoveredGuideIndex_ = guideIndex;
      guideSelected_ = true;
      selectedGuideIndex_ = guideIndex;
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
    } else if (event->type() == QEvent::Leave) {
      hoveredGuideIndex_ = -1;
      updateFocusStyle();
    } else if (event->type() == QEvent::MouseButtonPress) {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        setInputMode(InputMode::Mouse);
        guideSelected_ = true;
        selectedGuideIndex_ = guideIndex;
        activateGuideItem();
        return true;
      }
    }
  }

  const int railIndex = railIndexForObject(watched);
  const int itemIndex = itemIndexForObject(watched);
  if (railIndex >= 0 && itemIndex >= 0) {
    if (event->type() == QEvent::Enter) {
      setInputMode(InputMode::Mouse);
      hoveredRailIndex_ = railIndex;
      hoveredItemIndex_ = itemIndex;
      setFocusedItem(railIndex, itemIndex, false);
    } else if (event->type() == QEvent::Leave) {
      if (hoveredRailIndex_ == railIndex && hoveredItemIndex_ == itemIndex) {
        hoveredRailIndex_ = -1;
        hoveredItemIndex_ = -1;
        updateFocusStyle();
      }
    } else if (event->type() == QEvent::MouseButtonPress) {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        setInputMode(InputMode::Mouse);
        setFocusedItem(railIndex, itemIndex, false);
        activateFocused();
        return true;
      }
    }
  }

  return QWidget::eventFilter(watched, event);
}

void YouTubeBrowsePage::mouseMoveEvent(QMouseEvent *event) {
  setInputMode(InputMode::Mouse);
  if (sidebar_ && !sidebar_->geometry().contains(event->pos()) &&
      guideSelected_ && focusedRailIndex_ >= 0) {
    hoveredGuideIndex_ = -1;
    guideSelected_ = false;
    updateSidebarState(true);
    updateFocusStyle();
  }
  QWidget::mouseMoveEvent(event);
}

void YouTubeBrowsePage::leaveEvent(QEvent *event) {
  clearHover();
  if (focusedRailIndex_ >= 0) {
    guideSelected_ = false;
    updateSidebarState(true);
    updateFocusStyle();
  }
  QWidget::leaveEvent(event);
}

} // namespace GUI
} // namespace AIO