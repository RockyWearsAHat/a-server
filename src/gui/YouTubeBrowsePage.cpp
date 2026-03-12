#include "gui/YouTubeBrowsePage.h"

#include "gui/ThumbnailCache.h"
#include "gui/ThumbnailFillLabel.h"
#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
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
constexpr auto kTileBaseRectProperty = "aio_tile_base_rect";
constexpr auto kThumbnailUrlProperty = "aio_thumbnail_url";
constexpr int kCollapsedSidebarWidth = 96;
constexpr int kExpandedSidebarWidth = 312;
constexpr int kGuideTextWidth = 212;
constexpr int kGuideIconFrameSize = 48;
constexpr int kGuideIconGlyphSize = 22;
constexpr int kTileSlotPadding = 24;
constexpr int kTileFocusGrow = 14;
constexpr int kMaxHomeDiscoveryDepth = 4;

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

int visibleTilesForWidth(int viewportWidth) {
  if (viewportWidth >= 2600) {
    return 5;
  }
  if (viewportWidth >= 1880) {
    return 4;
  }
  if (viewportWidth >= 1260) {
    return 3;
  }
  if (viewportWidth >= 860) {
    return 2;
  }
  return 1;
}

int tileWidthForViewport(int viewportWidth) {
  const int visibleTiles = visibleTilesForWidth(viewportWidth);
  const int spacing = 24;
  const int availableWidth =
      std::max(360, viewportWidth - ((visibleTiles - 1) * spacing) - 40);
  return std::clamp(availableWidth / std::max(1, visibleTiles), 292, 468);
}

QString scrubDescription(QString text) {
  text.replace(QRegularExpression(QStringLiteral("https?://\\S+")), QString());
  text.replace(QRegularExpression(QStringLiteral("(?i)download:?\\s*")),
               QString());
  text.replace(QRegularExpression(QStringLiteral("(?i)stream/")), QString());
  return text.simplified();
}

QString conciseMeta(const AIO::Streaming::VideoContent &item) {
  const QString category = QString::fromStdString(item.category).simplified();
  if (!category.isEmpty()) {
    return category;
  }

  QString description =
      scrubDescription(QString::fromStdString(item.description));
  if (!description.isEmpty()) {
    return elideText(description, 40);
  }

  return QStringLiteral("Play on YouTube");
}

QRect expandedRectFor(const QRect &baseRect) {
  return baseRect.adjusted(-kTileFocusGrow, -kTileFocusGrow, kTileFocusGrow,
                           kTileFocusGrow);
}

QGraphicsDropShadowEffect *ensureShadowEffect(QWidget *widget) {
  if (!widget) {
    return nullptr;
  }

  auto *shadow =
      qobject_cast<QGraphicsDropShadowEffect *>(widget->graphicsEffect());
  if (!shadow) {
    shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setBlurRadius(0.0);
    shadow->setOffset(0.0, 0.0);
    shadow->setColor(Qt::transparent);
    widget->setGraphicsEffect(shadow);
  }
  return shadow;
}

void applyThumbnailIfAvailable(ThumbnailFillLabel *label, const QString &url) {
  if (!label || url.isEmpty()) {
    return;
  }

  QPixmap pixmap;
  if (ThumbnailCache::instance().tryGet(url, &pixmap)) {
    label->setSourcePixmap(pixmap);
  }
}

void animateRect(QWidget *widget, const QRect &targetRect, int duration) {
  if (!widget || widget->geometry() == targetRect) {
    return;
  }

  if (auto *existing = widget->findChild<QPropertyAnimation *>(
          QStringLiteral("aioTileGeometryAnimation"))) {
    existing->stop();
    existing->deleteLater();
  }

  auto *animation = new QPropertyAnimation(widget, "geometry", widget);
  animation->setObjectName(QStringLiteral("aioTileGeometryAnimation"));
  animation->setDuration(duration);
  animation->setStartValue(widget->geometry());
  animation->setEndValue(targetRect);
  animation->setEasingCurve(QEasingCurve::OutCubic);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void animateShadow(QGraphicsDropShadowEffect *shadow, qreal blurRadius,
                   const QPointF &offset, const QColor &color, int duration) {
  if (!shadow) {
    return;
  }

  auto restartAnimation = [&](const QString &name, const QByteArray &property,
                              const QVariant &endValue) {
    if (auto *existing = shadow->findChild<QPropertyAnimation *>(name)) {
      existing->stop();
      existing->deleteLater();
    }

    auto *animation = new QPropertyAnimation(shadow, property, shadow);
    animation->setObjectName(name);
    animation->setDuration(duration);
    animation->setEndValue(endValue);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
  };

  restartAnimation(QStringLiteral("aioTileShadowBlurAnimation"), "blurRadius",
                   blurRadius);
  restartAnimation(QStringLiteral("aioTileShadowOffsetAnimation"), "offset",
                   offset);
  shadow->setColor(color);
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
  sidebar_->setMinimumWidth(kCollapsedSidebarWidth);
  sidebar_->setMaximumWidth(kExpandedSidebarWidth);
  sidebar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
  auto *sidebarLayout = new QVBoxLayout(sidebar_);
  sidebarLayout->setContentsMargins(16, 24, 16, 24);
  sidebarLayout->setSpacing(18);

  sidebarTitleLabel_ = new QLabel("YT", sidebar_);
  sidebarTitleLabel_->setObjectName("aioYouTubeSidebarBrand");
  sidebarTitleLabel_->setAlignment(Qt::AlignCenter);
  sidebarTitleLabel_->setFixedSize(64, 64);
  sidebarLayout->addWidget(sidebarTitleLabel_, 0, Qt::AlignHCenter);

  guideList_ = new QWidget(sidebar_);
  guideLayout_ = new QVBoxLayout(guideList_);
  guideLayout_->setContentsMargins(0, 0, 0, 0);
  guideLayout_->setSpacing(12);
  sidebarLayout->addWidget(guideList_);
  sidebar_->installEventFilter(this);
  guideList_->installEventFilter(this);
  sidebarLayout->addStretch();

  auto *mainShell = new QWidget(this);
  mainShell->setObjectName("aioYouTubeMainShell");
  auto *mainLayout = new QVBoxLayout(mainShell);
  mainLayout->setContentsMargins(32, 24, 36, 36);
  mainLayout->setSpacing(24);

  topBar_ = new QWidget(mainShell);
  topBar_->setObjectName("aioTopBar");
  topBar_->installEventFilter(this);
  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(24, 18, 24, 18);
  barLayout->setSpacing(18);

  backButton_ = new QPushButton("Back", topBar_);
  backButton_->setFocusPolicy(Qt::NoFocus);
  backButton_->setProperty("variant", "secondary");

  homeButton_ = new QPushButton("Home", topBar_);
  homeButton_->setFocusPolicy(Qt::NoFocus);
  homeButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("YouTube", topBar_);
  titleLabel_->setProperty("role", "ytBrowseHeaderTitle");

  searchEdit_ = new QLineEdit(topBar_);
  searchEdit_->setPlaceholderText("Search YouTube");
  searchEdit_->setObjectName("aioYouTubeSearch");
  searchEdit_->setMinimumWidth(340);
  searchEdit_->setMaximumWidth(620);

  searchButton_ = new QPushButton("Search", topBar_);
  searchButton_->setFocusPolicy(Qt::NoFocus);
  searchButton_->setProperty("variant", "secondary");
  searchButton_->setObjectName("aioYouTubeSearchButton");

  accountButton_ = new QPushButton(topBar_);
  accountButton_->setObjectName("aioYouTubeAccountButton");
  accountButton_->setFocusPolicy(Qt::NoFocus);
  accountButton_->setCursor(Qt::PointingHandCursor);
  accountButton_->setFixedSize(48, 48);
  accountButton_->setText("YT");
  accountButton_->setProperty("variant", "secondary");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addSpacing(10);
  barLayout->addWidget(titleLabel_);
  barLayout->addStretch();
  barLayout->addWidget(searchEdit_);
  barLayout->addWidget(searchButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(accountButton_);

  auto *heroRow = new QWidget(mainShell);
  heroRow->setObjectName("aioYouTubeHeroRow");
  auto *heroRowLayout = new QHBoxLayout(heroRow);
  heroRowLayout->setContentsMargins(0, 0, 0, 0);
  heroRowLayout->setSpacing(16);

  heroCard_ = new QFrame(heroRow);
  heroCard_->setObjectName("aioYouTubeHeroCard");
  heroCard_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  heroCard_->setMaximumHeight(198);

  auto *heroLayout = new QHBoxLayout(heroCard_);
  heroLayout->setContentsMargins(22, 18, 22, 18);
  heroLayout->setSpacing(16);

  auto *heroCopyColumn = new QVBoxLayout();
  heroCopyColumn->setContentsMargins(0, 0, 0, 0);
  heroCopyColumn->setSpacing(12);

  heroEyebrowLabel_ = new QLabel("DISCOVER", heroCard_);
  heroEyebrowLabel_->setObjectName("aioYouTubeHeroEyebrow");

  heroTitleLabel_ = new QLabel("Popular on YouTube", heroCard_);
  heroTitleLabel_->setObjectName("aioYouTubeHeroTitle");
  heroTitleLabel_->setWordWrap(true);

  heroBodyLabel_ = new QLabel(heroCard_);
  heroBodyLabel_->setObjectName("aioYouTubeHeroBody");
  heroBodyLabel_->setWordWrap(true);

  auto makeHeroChip = [&](const QString &text) {
    auto *chip = new QLabel(text, heroCard_);
    chip->setProperty("role", "ytHeroChip");
    chip->setAlignment(Qt::AlignCenter);
    return chip;
  };

  auto *heroChipRow = new QHBoxLayout();
  heroChipRow->setContentsMargins(0, 4, 0, 0);
  heroChipRow->setSpacing(10);
  heroPrimaryChip_ = makeHeroChip("0 rows");
  heroSecondaryChip_ = makeHeroChip("Signed out");
  heroTertiaryChip_ = makeHeroChip("Controller ready");
  heroChipRow->addWidget(heroPrimaryChip_);
  heroChipRow->addWidget(heroSecondaryChip_);
  heroChipRow->addWidget(heroTertiaryChip_);
  heroChipRow->addStretch(1);

  heroCopyColumn->addWidget(heroEyebrowLabel_);
  heroCopyColumn->addWidget(heroTitleLabel_);
  heroCopyColumn->addWidget(heroBodyLabel_);
  heroCopyColumn->addLayout(heroChipRow);
  heroCopyColumn->addStretch(1);

  heroSpotlightCard_ = new QFrame(heroCard_);
  heroSpotlightCard_->setObjectName("aioYouTubeHeroSpotlight");
  heroSpotlightCard_->setMinimumWidth(220);
  heroSpotlightCard_->setMaximumWidth(280);

  auto *heroSpotlightLayout = new QVBoxLayout(heroSpotlightCard_);
  heroSpotlightLayout->setContentsMargins(18, 18, 18, 18);
  heroSpotlightLayout->setSpacing(8);

  heroSpotlightEyebrowLabel_ = new QLabel("FOCUS", heroSpotlightCard_);
  heroSpotlightEyebrowLabel_->setObjectName("aioYouTubeHeroSpotlightEyebrow");

  heroSpotlightTitleLabel_ =
      new QLabel("Move into a row to preview it", heroSpotlightCard_);
  heroSpotlightTitleLabel_->setObjectName("aioYouTubeHeroSpotlightTitle");
  heroSpotlightTitleLabel_->setWordWrap(true);

  heroSpotlightMetaLabel_ =
      new QLabel("Use Right from the guide to land on the first playable tile.",
                 heroSpotlightCard_);
  heroSpotlightMetaLabel_->setObjectName("aioYouTubeHeroSpotlightMeta");
  heroSpotlightMetaLabel_->setWordWrap(true);

  heroSpotlightLayout->addWidget(heroSpotlightEyebrowLabel_);
  heroSpotlightLayout->addWidget(heroSpotlightTitleLabel_);
  heroSpotlightLayout->addWidget(heroSpotlightMetaLabel_);
  heroSpotlightLayout->addStretch(1);

  heroLayout->addLayout(heroCopyColumn, 1);
  heroLayout->addWidget(heroSpotlightCard_, 0, Qt::AlignTop);

  authCard_ = new QFrame(heroRow);
  authCard_->setObjectName("aioYouTubeAccountStrip");
  authCard_->setProperty(kAuthCardProperty, true);
  authCard_->setFocusPolicy(Qt::NoFocus);
  authCard_->setMinimumWidth(320);
  authCard_->setMaximumWidth(380);
  authCard_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  authCard_->installEventFilter(this);

  auto *authLayout = new QVBoxLayout(authCard_);
  authLayout->setContentsMargins(28, 22, 28, 22);
  authLayout->setSpacing(14);

  auto *authTopRow = new QHBoxLayout();
  authTopRow->setSpacing(20);

  auto *authCopyColumn = new QVBoxLayout();
  authCopyColumn->setContentsMargins(0, 0, 0, 0);
  authCopyColumn->setSpacing(6);

  authAvatarLabel_ = new QLabel(authCard_);
  authAvatarLabel_->setObjectName("aioYouTubeAccountAvatar");
  authAvatarLabel_->setAlignment(Qt::AlignCenter);
  authAvatarLabel_->setFixedSize(52, 52);
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
  authQrLabel_->setFixedSize(132, 132);
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

  heroRowLayout->addWidget(heroCard_, 3);
  heroRowLayout->addWidget(authCard_, 2, Qt::AlignTop);

  auto *contentWrapper = new QWidget(mainShell);
  contentWrapper->setObjectName("aioYouTubeContentShell");
  auto *contentLayout = new QVBoxLayout(contentWrapper);
  contentLayout->setContentsMargins(0, 10, 0, 0);
  contentLayout->setSpacing(26);

  scroll_ = new QScrollArea(contentWrapper);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->installEventFilter(this);

  contentHost_ = new QWidget(scroll_);
  contentLayout_ = new QVBoxLayout(contentHost_);
  contentLayout_->setContentsMargins(0, 10, 0, 36);
  contentLayout_->setSpacing(44);

  scroll_->setWidget(contentHost_);
  contentLayout->addWidget(scroll_, 1);

  mainLayout->addWidget(topBar_);
  mainLayout->addWidget(heroRow);
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
  connect(accountButton_, &QPushButton::clicked, this,
          &YouTubeBrowsePage::performAuthPrimaryAction);
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
          [this](const QString &url) {
            refreshLoadedThumbnail(url);
            refreshAuthArtwork(url);
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

void YouTubeBrowsePage::scheduleContentRebuild() {
  if (contentRebuildScheduled_ || rails_.empty()) {
    return;
  }

  if (scroll_ && scroll_->verticalScrollBar()) {
    restoreVerticalScrollValue_ = scroll_->verticalScrollBar()->value();
  }
  restoreHorizontalScrollValues_.clear();
  restoreHorizontalScrollValues_.reserve(railScrolls_.size());
  for (auto *railScroll : railScrolls_) {
    restoreHorizontalScrollValues_.push_back(
        railScroll && railScroll->horizontalScrollBar()
            ? railScroll->horizontalScrollBar()->value()
            : 0);
  }

  contentRebuildScheduled_ = true;
  QTimer::singleShot(0, this, [this]() {
    contentRebuildScheduled_ = false;
    rebuildContent();
  });
}

void YouTubeBrowsePage::refreshLoadedThumbnail(const QString &url) {
  if (!contentHost_ || url.trimmed().isEmpty()) {
    return;
  }

  const auto thumbs = contentHost_->findChildren<QLabel *>(
      QStringLiteral("thumb"), Qt::FindChildrenRecursively);
  for (auto *thumb : thumbs) {
    if (!thumb || thumb->property(kThumbnailUrlProperty).toString() != url) {
      continue;
    }
    if (auto *fillThumb = dynamic_cast<ThumbnailFillLabel *>(thumb)) {
      applyThumbnailIfAvailable(fillThumb, url);
    }
  }
}

void YouTubeBrowsePage::refreshAuthArtwork(const QString &url) {
  if (url.trimmed().isEmpty()) {
    return;
  }

  if ((!accountAvatarUrl_.isEmpty() && accountAvatarUrl_ == url) ||
      qrImageUrlForSession() == url) {
    rebuildAuthCard();
  }
}

void YouTubeBrowsePage::setLoadingState(bool loading, const QString &text) {
  setStatus(text);
  searchButton_->setEnabled(!loading);
  authPrimaryButton_->setEnabled(!loading);
}

QString
YouTubeBrowsePage::summaryFor(const AIO::Streaming::VideoContent &item) const {
  return conciseMeta(item);
}

void YouTubeBrowsePage::loadTrending() { refreshHome(); }

void YouTubeBrowsePage::refreshHome() {
  if (!youTube_) {
    setStatus("YouTube service not available.");
    return;
  }

  setLoadingState(true, "Loading YouTube home...");
  loadingMoreHome_ = false;
  homeDiscoveryDepth_ = 0;
  const uint64_t requestId = ++requestSerial_;
  const int railSize = std::max(itemsPerRail() * 8, 28);
  QPointer<YouTubeBrowsePage> guard(this);
  auto *service = youTube_;

  std::thread([guard, requestId, railSize, service]() {
    const auto rails = service
                           ? service->getHomeRails(railSize, 0)
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

void YouTubeBrowsePage::requestAdditionalDiscoveryIfNeeded(
    int targetRailIndex) {
  if (loadingMoreHome_ || targetRailIndex < 0 || rails_.empty() ||
      homeDiscoveryDepth_ >= kMaxHomeDiscoveryDepth ||
      searchEdit_->hasFocus() || !searchEdit_->text().trimmed().isEmpty()) {
    return;
  }

  const int triggerRail = std::max(0, static_cast<int>(rails_.size()) - 2);
  if (targetRailIndex < triggerRail) {
    return;
  }

  loadMoreHomeRows();
}

void YouTubeBrowsePage::loadMoreHomeRows() {
  if (!youTube_ || loadingMoreHome_ ||
      homeDiscoveryDepth_ >= kMaxHomeDiscoveryDepth) {
    return;
  }

  loadingMoreHome_ = true;
  const int nextDepth = homeDiscoveryDepth_ + 1;
  const uint64_t requestId = ++requestSerial_;
  const int railSize = std::max(itemsPerRail() * 8, 28);
  QPointer<YouTubeBrowsePage> guard(this);
  auto *service = youTube_;
  setStatus(QStringLiteral("Loading more rows..."));

  std::thread([guard, requestId, railSize, nextDepth, service]() {
    const auto rails = service
                           ? service->getHomeRails(railSize, nextDepth)
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
        [guard, requestId, nextDepth, rails, heroBody, accountLabel]() {
          if (!guard || requestId != guard->requestSerial_) {
            return;
          }
          guard->homeDiscoveryDepth_ = nextDepth;
          guard->loadingMoreHome_ = false;
          guard->setRails(rails, heroBody, accountLabel, true);
          guard->setStatus(QStringLiteral("Loaded %1 rows").arg(rails.size()));
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
      auto results = service->search(searchQuery, 40);
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
    const QString &heroBody, const QString &accountLabel, bool preserveFocus) {
  const QString previousGuideKey =
      selectedGuideIndex_ >= 0 &&
              selectedGuideIndex_ < static_cast<int>(guideItems_.size())
          ? guideItems_[selectedGuideIndex_].key
          : QString();
  const QString previousRailKey =
      !guideSelected_ && focusedRailIndex_ >= 0 &&
              focusedRailIndex_ < static_cast<int>(rails_.size())
          ? rails_[focusedRailIndex_].key
          : QString();
  const int previousItemIndex = focusedItemIndex_;
  const bool shouldRestoreRailFocus = preserveFocus && !guideSelected_;

  if (preserveFocus && scroll_ && scroll_->verticalScrollBar()) {
    restoreVerticalScrollValue_ = scroll_->verticalScrollBar()->value();
    restoreHorizontalScrollValues_.clear();
    restoreHorizontalScrollValues_.reserve(railScrolls_.size());
    for (auto *railScroll : railScrolls_) {
      restoreHorizontalScrollValues_.push_back(
          railScroll && railScroll->horizontalScrollBar()
              ? railScroll->horizontalScrollBar()->value()
              : 0);
    }
  } else {
    restoreVerticalScrollValue_ = -1;
    restoreHorizontalScrollValues_.clear();
  }

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
  heroBody_ = heroBody.trimmed();
  currentSectionTitle_ =
      rails_.empty() ? QStringLiteral("For you") : rails_.front().title;

  rebuildGuide();
  railSelections_.assign(rails_.size(), 0);
  focusedRailIndex_ = rails_.empty() ? -1 : 0;
  focusedItemIndex_ = 0;
  guideSelected_ = true;
  if (shouldRestoreRailFocus && !previousRailKey.isEmpty()) {
    for (int index = 0; index < static_cast<int>(rails_.size()); ++index) {
      if (rails_[index].key != previousRailKey || rails_[index].items.empty()) {
        continue;
      }
      focusedRailIndex_ = index;
      focusedItemIndex_ =
          std::clamp(previousItemIndex, 0,
                     static_cast<int>(rails_[index].items.size()) - 1);
      railSelections_[index] = focusedItemIndex_;
      guideSelected_ = false;
      break;
    }
  } else if (preserveFocus && !previousGuideKey.isEmpty()) {
    for (int index = 0; index < static_cast<int>(guideItems_.size()); ++index) {
      if (guideItems_[index].key == previousGuideKey) {
        selectedGuideIndex_ = index;
        break;
      }
    }
  }
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

  ++sidebarAnimationEpoch_;

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
    buttonLayout->setContentsMargins(8, 6, 8, 6);
    buttonLayout->setSpacing(10);

    auto *iconLabel = new QLabel(button);
    iconLabel->setObjectName("aioYouTubeGuideIconWrap");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(kGuideIconFrameSize, kGuideIconFrameSize);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *textClip = new QWidget(button);
    textClip->setObjectName("guideTextClip");
    textClip->setMaximumWidth(guideSelected_ ? kGuideTextWidth : 0);
    textClip->setMinimumWidth(0);
    textClip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    textClip->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto *textClipLayout = new QHBoxLayout(textClip);
    textClipLayout->setContentsMargins(0, 0, 0, 0);
    textClipLayout->setSpacing(0);

    auto *textLabel = new QLabel(guideItems_[index].title, textClip);
    textLabel->setObjectName("guideText");
    textLabel->setProperty("role", "guideText");
    textLabel->setMinimumWidth(kGuideTextWidth);
    textLabel->setMaximumWidth(kGuideTextWidth);
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
  if (accountButton_) {
    accountButton_->setIcon(QIcon());
    accountButton_->setText(accountLabel_.isEmpty()
                                ? QStringLiteral("YT")
                                : accountLabel_.left(1).toUpper());
  }

  if (!accountAvatarUrl_.isEmpty()) {
    QPixmap avatarPixmap;
    if (ThumbnailCache::instance().tryGet(accountAvatarUrl_, &avatarPixmap)) {
      authAvatarLabel_->setPixmap(circularPixmap(avatarPixmap, 48));
      authAvatarLabel_->setText(QString());
      if (accountButton_) {
        accountButton_->setIcon(QIcon(circularPixmap(avatarPixmap, 40)));
        accountButton_->setIconSize(QSize(40, 40));
        accountButton_->setText(QString());
      }
    } else {
      ThumbnailCache::instance().request(accountAvatarUrl_);
    }
  }

  if (accountButton_) {
    accountButton_->setToolTip(signedIn ? QStringLiteral("YouTube account")
                                        : QStringLiteral("Sign in to YouTube"));
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
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(true);
    authBodyLabel_->setVisible(true);
  } else if (signedIn) {
    const QString accountName = accountLabel_.isEmpty()
                                    ? QStringLiteral("your account")
                                    : accountLabel_;
    authTitleLabel_->setText(
        QStringLiteral("Signed in as %1").arg(accountName));
    authBodyLabel_->setText(
        "Personalized rows, subscriptions, and watch progress are active.");
    authHintLabel_->setText(QStringLiteral("%1 rows ready").arg(rails_.size()));
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Refresh");
    authSecondaryButton_->setText("Sign out");
    authSecondaryButton_->setVisible(true);
    authQrLabel_->setVisible(false);
    authCodeLabel_->setVisible(false);
    authUrlLabel_->setVisible(false);
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
    authBodyLabel_->setVisible(true);
  } else if (!canStartDeviceAuth) {
    authTitleLabel_->setText("Sign-in server unavailable");
    authBodyLabel_->setText("Start the YouTube auth server to enable QR "
                            "sign-in and personalized rows.");
    authHintLabel_->setText("Public browsing still works while signed out.");
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Refresh");
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
    authBodyLabel_->setVisible(true);
  } else {
    authTitleLabel_->setText("Sign in for subscriptions");
    authBodyLabel_->setText(
        "Unlock subscriptions, watch later, likes, and watch progress while "
        "the public home feed stays available right now.");
    authHintLabel_->setText(
        "Use the profile button in the top-right or select Sign in here.");
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Sign in");
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
    authBodyLabel_->setVisible(true);
  }

  updateFocusStyle();
}

void YouTubeBrowsePage::updateHeroSpotlight() {
  if (!heroEyebrowLabel_ || !heroTitleLabel_ || !heroBodyLabel_ ||
      !heroPrimaryChip_ || !heroSecondaryChip_ || !heroTertiaryChip_ ||
      !heroSpotlightEyebrowLabel_ || !heroSpotlightTitleLabel_ ||
      !heroSpotlightMetaLabel_) {
    return;
  }

  const bool signedIn = youTube_ && youTube_->hasOAuthAccess();
  int totalVideos = 0;
  for (const auto &rail : rails_) {
    totalVideos += static_cast<int>(rail.items.size());
  }

  QString eyebrow =
      signedIn ? QStringLiteral("DISCOVER") : QStringLiteral("GUEST BROWSE");
  QString title = currentSectionTitle_.trimmed();
  if (title.isEmpty()) {
    title = QStringLiteral("Popular on YouTube");
  }

  QString body = heroBody_.trimmed();
  QString spotlightEyebrow = QStringLiteral("NOW SELECTED");
  QString spotlightTitle =
      rails_.empty() ? QStringLiteral("Home is warming up")
                     : QStringLiteral("%1 rows loaded").arg(rails_.size());
  QString spotlightMeta =
      statusLabel_ ? statusLabel_->text().trimmed() : QString();

  QStringList chipLabels;
  chipLabels.push_back(QStringLiteral("All"));
  for (const auto &rail : rails_) {
    if (rail.title.trimmed().isEmpty()) {
      continue;
    }
    if (!chipLabels.contains(rail.title)) {
      chipLabels.push_back(rail.title);
    }
    if (chipLabels.size() >= 4) {
      break;
    }
  }
  while (chipLabels.size() < 4) {
    chipLabels.push_back(chipLabels.size() == 1 ? QStringLiteral("Trending")
                                                : QStringLiteral("Gaming"));
  }

  if (guideSelected_ && selectedGuideIndex_ >= 0 &&
      selectedGuideIndex_ < static_cast<int>(guideItems_.size())) {
    const auto &guide = guideItems_[selectedGuideIndex_];
    if (!guide.title.trimmed().isEmpty()) {
      title = guide.title;
    }

    if (guide.key == QStringLiteral("search")) {
      eyebrow = QStringLiteral("SEARCH");
      if (body.isEmpty()) {
        body = QStringLiteral(
            "Search across trending picks, personalized rows, and direct "
            "results without leaving the TV shell.");
      }
      spotlightTitle = QStringLiteral("Search the full YouTube catalog");
      spotlightMeta =
          QStringLiteral("Press Right to move into the first playable tile.");
    } else if (guide.key == QStringLiteral("connect") ||
               guide.key == QStringLiteral("account")) {
      eyebrow =
          signedIn ? QStringLiteral("ACCOUNT") : QStringLiteral("SIGN IN");
      if (authTitleLabel_ && !authTitleLabel_->text().trimmed().isEmpty()) {
        title = authTitleLabel_->text().trimmed();
      }
      if (authBodyLabel_ && !authBodyLabel_->text().trimmed().isEmpty()) {
        body = authBodyLabel_->text().trimmed();
      }
      spotlightEyebrow = QStringLiteral("ACCOUNT");
      spotlightTitle = authPrimaryButton_ ? authPrimaryButton_->text().trimmed()
                                          : QStringLiteral("Sign in");
      spotlightMeta =
          authHintLabel_ && !authHintLabel_->text().trimmed().isEmpty()
              ? authHintLabel_->text().trimmed()
              : QStringLiteral("The profile button mirrors this surface.");
    } else {
      spotlightTitle =
          rails_.empty() ? QStringLiteral("Browse is ready") : guide.title;
      if (spotlightMeta.isEmpty()) {
        spotlightMeta = QStringLiteral(
            "Use Right to move from the guide into the highlighted row.");
      }
    }
  }

  if (authCardSelected_) {
    eyebrow = signedIn ? QStringLiteral("ACCOUNT") : QStringLiteral("SIGN IN");
    if (authTitleLabel_ && !authTitleLabel_->text().trimmed().isEmpty()) {
      title = authTitleLabel_->text().trimmed();
    }
    if (authBodyLabel_ && !authBodyLabel_->text().trimmed().isEmpty()) {
      body = authBodyLabel_->text().trimmed();
    }
    spotlightEyebrow = QStringLiteral("ACCOUNT");
    spotlightTitle =
        authPrimaryButton_ && !authPrimaryButton_->text().trimmed().isEmpty()
            ? authPrimaryButton_->text().trimmed()
            : QStringLiteral("Open account actions");
    spotlightMeta =
        authHintLabel_ && !authHintLabel_->text().trimmed().isEmpty()
            ? authHintLabel_->text().trimmed()
            : QStringLiteral("This panel stays controller-friendly while "
                             "content keeps streaming below.");
  } else if (!guideSelected_ && focusedRailIndex_ >= 0 &&
             focusedRailIndex_ < static_cast<int>(rails_.size()) &&
             focusedItemIndex_ >= 0 &&
             focusedItemIndex_ <
                 static_cast<int>(rails_[focusedRailIndex_].items.size())) {
    const auto &rail = rails_[focusedRailIndex_];
    const auto &item = rail.items[focusedItemIndex_];
    const QString itemTitle = QString::fromStdString(item.title).simplified();
    const QString itemDescription =
        scrubDescription(QString::fromStdString(item.description));
    const QString itemMeta = summaryFor(item);
    const QString durationText = formatDuration(item.durationSeconds);

    eyebrow = rail.title.trimmed().isEmpty() ? QStringLiteral("SPOTLIGHT")
                                             : rail.title.trimmed().toUpper();
    if (!itemTitle.isEmpty()) {
      title = itemTitle;
    }
    if (!itemDescription.isEmpty()) {
      body = elideText(itemDescription, 180);
    } else if (!itemMeta.trimmed().isEmpty()) {
      body = itemMeta;
    }

    spotlightEyebrow = QStringLiteral("PLAY NOW");
    spotlightTitle = rail.title.trimmed().isEmpty() ? QStringLiteral("Play now")
                                                    : rail.title.trimmed();
    spotlightMeta = durationText;
    if (!itemMeta.trimmed().isEmpty()) {
      spotlightMeta =
          spotlightMeta.isEmpty()
              ? itemMeta
              : QStringLiteral("%1  %2").arg(durationText, itemMeta);
    }
  }

  if (body.isEmpty()) {
    body = signedIn
               ? QStringLiteral(
                     "Your account rails, history, and recommendations stay "
                     "ready for fast controller-first playback.")
               : QStringLiteral(
                     "Browse public picks instantly, then sign in whenever "
                     "you want subscriptions, likes, and watch progress.");
  }

  if (spotlightMeta.isEmpty()) {
    spotlightMeta = QStringLiteral(
        "Use Left and Right to change surfaces without losing context.");
  }

  heroEyebrowLabel_->setText(eyebrow);
  heroTitleLabel_->setText(title);
  heroBodyLabel_->setText(body);
  heroBodyLabel_->setVisible(!body.trimmed().isEmpty());

  auto applyChip = [&](QLabel *chip, const QString &text, bool active) {
    if (!chip) {
      return;
    }
    chip->setText(text);
    chip->setProperty("active", active);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    chip->update();
  };

  applyChip(heroPrimaryChip_, chipLabels.value(0, QStringLiteral("All")),
            currentSectionTitle_.trimmed().isEmpty() || guideSelected_);
  applyChip(heroSecondaryChip_, chipLabels.value(1, QStringLiteral("Trending")),
            currentSectionTitle_.trimmed() == chipLabels.value(1));
  applyChip(heroTertiaryChip_, chipLabels.value(2, QStringLiteral("Gaming")),
            currentSectionTitle_.trimmed() == chipLabels.value(2));
  heroSpotlightEyebrowLabel_->setText(spotlightEyebrow);
  heroSpotlightTitleLabel_->setText(spotlightTitle);
  heroSpotlightMetaLabel_->setText(spotlightMeta);
  heroSpotlightCard_->setVisible(true);
}

void YouTubeBrowsePage::updateSidebarState(bool animated) {
  if (!sidebar_) {
    return;
  }

  const bool shouldExpand = guideSelected_;
  sidebarExpanded_ = shouldExpand;
  const uint64_t animationEpoch = ++sidebarAnimationEpoch_;

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

    const int targetTextWidth = shouldExpand ? kGuideTextWidth : 0;
    const qreal targetOpacity = shouldExpand ? 1.0 : 0.0;
    const int staggerDelay = index * 18;
    if (animated) {
      QPointer<QWidget> textClipGuard(textClip);
      QPointer<QLabel> textLabelGuard(textLabel);
      QPointer<QGraphicsOpacityEffect> effectGuard(effect);
      QTimer::singleShot(
          staggerDelay, this,
          [this, animationEpoch, textClipGuard, textLabelGuard, effectGuard,
           targetTextWidth, targetOpacity]() {
            if (animationEpoch != sidebarAnimationEpoch_) {
              return;
            }

            auto *textClip = textClipGuard.data();
            auto *textLabel = textLabelGuard.data();
            auto *effect = effectGuard.data();
            if (!textClip || !textLabel || !effect) {
              return;
            }

            auto *widthAnim =
                new QPropertyAnimation(textClip, "maximumWidth", textClip);
            widthAnim->setDuration(210);
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
            opacityAnim->setDuration(180);
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

  const int targetSidebarWidth =
      shouldExpand ? kExpandedSidebarWidth : kCollapsedSidebarWidth;
  if (animated) {
    auto animateSidebarWidth = [&](const QByteArray &propertyName, int start,
                                   int end) {
      auto *animation =
          new QPropertyAnimation(sidebar_, propertyName, sidebar_);
      animation->setDuration(240);
      animation->setStartValue(start);
      animation->setEndValue(end);
      animation->setEasingCurve(QEasingCurve::OutCubic);
      QObject::connect(animation, &QVariantAnimation::valueChanged, this,
                       [this]() {
                         if (sidebar_) {
                           sidebar_->updateGeometry();
                         }
                         if (layout()) {
                           layout()->activate();
                         }
                       });
      animation->start(QAbstractAnimation::DeleteWhenStopped);
    };

    animateSidebarWidth("minimumWidth", sidebar_->minimumWidth(),
                        targetSidebarWidth);
    animateSidebarWidth("maximumWidth", sidebar_->maximumWidth(),
                        targetSidebarWidth);
  } else {
    sidebar_->setMinimumWidth(targetSidebarWidth);
    sidebar_->setMaximumWidth(targetSidebarWidth);
  }

  sidebar_->updateGeometry();
  if (layout()) {
    layout()->activate();
  }
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

  const int viewportWidth =
      scroll_ && scroll_->viewport() ? scroll_->viewport()->width() : width();
  const int tileWidth = tileWidthForViewport(viewportWidth);
  const int thumbHeight = (tileWidth * 9) / 16;
  const int tileHeight = thumbHeight + 112;
  contentViewportWidth_ = viewportWidth;
  contentTileWidth_ = tileWidth;

  for (int railIndex = 0; railIndex < static_cast<int>(rails_.size());
       ++railIndex) {
    const auto &rail = rails_[railIndex];

    auto *railBlock = new QWidget(contentHost_);
    railBlock->setObjectName("aioRailBlock");
    auto *railLayout = new QVBoxLayout(railBlock);
    railLayout->setContentsMargins(24, 22, 24, 24);
    railLayout->setSpacing(18);

    auto *header = new QWidget(railBlock);
    header->setObjectName("aioRailHeader");
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    auto *title = new QLabel(rail.title, header);
    title->setProperty("role", "ytSectionTitle");
    auto *subtitle = new QLabel(rail.subtitle, header);
    subtitle->setProperty("role", "ytSectionMeta");
    subtitle->setWordWrap(true);
    subtitle->setVisible(!rail.subtitle.trimmed().isEmpty());

    headerLayout->addWidget(title);
    headerLayout->addWidget(subtitle);

    auto *railScroll = new QScrollArea(railBlock);
    railScroll->setObjectName("aioRailScroller");
    railScroll->setWidgetResizable(true);
    railScroll->setFrameShape(QFrame::NoFrame);
    railScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setFocusPolicy(Qt::NoFocus);

    auto *railHost = new QWidget(railScroll);
    railHost->setObjectName("aioRailHost");
    auto *itemsLayout = new QHBoxLayout(railHost);
    itemsLayout->setContentsMargins(6, 4, 28, 4);
    itemsLayout->setSpacing(28);

    std::vector<QFrame *> tiles;
    tiles.reserve(rail.items.size());

    for (int itemIndex = 0; itemIndex < static_cast<int>(rail.items.size());
         ++itemIndex) {
      const auto &item = rail.items[itemIndex];
      auto *tileSlot = new QWidget(railHost);
      tileSlot->setObjectName("aioTileSlot");
      tileSlot->setFixedSize(tileWidth + (kTileSlotPadding * 2),
                             tileHeight + (kTileSlotPadding * 2));

      auto *tile = new QFrame(tileSlot);
      tile->setObjectName("aioTile");
      tile->setProperty(kRailIndexProperty, railIndex);
      tile->setProperty(kItemIndexProperty, itemIndex);
      tile->setProperty(kTileSelectedProperty, false);
      tile->setProperty(kTileHoveredProperty, false);
      tile->setProperty(
          kTileBaseRectProperty,
          QRect(kTileSlotPadding, kTileSlotPadding, tileWidth, tileHeight));
      tile->setGeometry(tile->property(kTileBaseRectProperty).toRect());
      tile->setFocusPolicy(Qt::NoFocus);
      tile->installEventFilter(this);

      auto *tileLayout = new QVBoxLayout(tile);
      tileLayout->setContentsMargins(0, 0, 0, 0);
      tileLayout->setSpacing(12);

      auto *thumbFrame = new QFrame(tile);
      thumbFrame->setObjectName("aioTileThumbFrame");
      thumbFrame->setFixedHeight(thumbHeight);
      thumbFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

      auto *thumbFrameLayout = new QGridLayout(thumbFrame);
      thumbFrameLayout->setContentsMargins(0, 0, 0, 0);
      thumbFrameLayout->setSpacing(0);

      auto *thumb = new ThumbnailFillLabel(thumbFrame);
      thumb->setObjectName("thumb");
      thumb->setProperty("role", "thumb");
      thumb->setMinimumHeight(thumbHeight);
      thumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      thumb->setText("Loading");
      thumb->setProperty(kRailIndexProperty, railIndex);
      thumb->setProperty(kItemIndexProperty, itemIndex);
      thumb->installEventFilter(this);

      const QString durationText = formatDuration(item.durationSeconds);

      auto *thumbOverlay = new QWidget(thumbFrame);
      thumbOverlay->setObjectName("aioTileThumbOverlay");
      auto *thumbOverlayLayout = new QHBoxLayout(thumbOverlay);
      thumbOverlayLayout->setContentsMargins(10, 0, 10, 10);
      thumbOverlayLayout->setSpacing(8);

      auto *durationChip = new QLabel(thumbOverlay);
      durationChip->setProperty("role", "tileDurationBadge");
      durationChip->setText(durationText);
      durationChip->setVisible(!durationText.isEmpty());

      thumbOverlayLayout->addStretch();
      thumbOverlayLayout->addWidget(durationChip);

      thumbFrameLayout->addWidget(thumb, 0, 0);
      thumbFrameLayout->addWidget(thumbOverlay, 0, 0,
                                  Qt::AlignLeft | Qt::AlignBottom);

      auto *copyWrap = new QWidget(tile);
      copyWrap->setObjectName("aioTileCopy");
      auto *copyLayout = new QVBoxLayout(copyWrap);
      copyLayout->setContentsMargins(16, 14, 16, 16);
      copyLayout->setSpacing(8);

      auto *itemTitle =
          new QLabel(elideText(QString::fromStdString(item.title), 68), tile);
      itemTitle->setProperty("role", "tileTitle");
      itemTitle->setWordWrap(true);
      itemTitle->setMaximumHeight(58);
      itemTitle->setProperty(kRailIndexProperty, railIndex);
      itemTitle->setProperty(kItemIndexProperty, itemIndex);
      itemTitle->installEventFilter(this);

      auto *meta = new QLabel(elideText(summaryFor(item), 60), tile);
      meta->setProperty("role", "tileMeta");
      meta->setWordWrap(true);
      meta->setMaximumHeight(42);
      meta->setVisible(!meta->text().trimmed().isEmpty());
      meta->setProperty(kRailIndexProperty, railIndex);
      meta->setProperty(kItemIndexProperty, itemIndex);
      meta->installEventFilter(this);

      const QString thumbUrl = QString::fromStdString(item.thumbnailUrl);
      thumb->setProperty(kThumbnailUrlProperty, thumbUrl);
      if (!thumbUrl.isEmpty()) {
        if (ThumbnailCache::instance().tryGet(thumbUrl, nullptr)) {
          applyThumbnailIfAvailable(thumb, thumbUrl);
        } else {
          ThumbnailCache::instance().request(thumbUrl);
        }
      }

      tileLayout->addWidget(thumbFrame);
      copyLayout->addWidget(itemTitle);
      copyLayout->addWidget(meta);
      copyLayout->addStretch();
      tileLayout->addWidget(copyWrap);

      itemsLayout->addWidget(tileSlot, 0, Qt::AlignTop);
      tiles.push_back(tile);
    }

    itemsLayout->addStretch();
    railHost->setLayout(itemsLayout);
    railScroll->setWidget(railHost);

    railLayout->addWidget(header);
    railLayout->addWidget(railScroll);

    contentLayout_->addWidget(railBlock);
    railScrolls_.push_back(railScroll);
    railTiles_.push_back(std::move(tiles));
  }

  contentLayout_->addStretch();
  if (scroll_ && scroll_->verticalScrollBar()) {
    scroll_->verticalScrollBar()->setValue(
        std::max(restoreVerticalScrollValue_, 0));
  }
  for (int railIndex = 0;
       railIndex < static_cast<int>(railScrolls_.size()) &&
       railIndex < static_cast<int>(restoreHorizontalScrollValues_.size());
       ++railIndex) {
    auto *railScroll = railScrolls_[railIndex];
    if (!railScroll || !railScroll->horizontalScrollBar()) {
      continue;
    }
    railScroll->horizontalScrollBar()->setValue(
        std::max(restoreHorizontalScrollValues_[railIndex], 0));
  }
  restoreVerticalScrollValue_ = -1;
  restoreHorizontalScrollValues_.clear();
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
  requestAdditionalDiscoveryIfNeeded(focusedRailIndex_);
}

void YouTubeBrowsePage::setFocusToAuthCard(bool ensureVisible) {
  if (guideItems_.empty()) {
    return;
  }
  guideSelected_ = !hasInteractiveAuthCard();
  authCardSelected_ = hasInteractiveAuthCard();
  for (int index = 0; index < static_cast<int>(guideItems_.size()); ++index) {
    if (guideItems_[index].key == QStringLiteral("account") ||
        guideItems_[index].key == QStringLiteral("connect")) {
      selectedGuideIndex_ = index;
      break;
    }
  }
  currentSectionTitle_ =
      accountLabel_.isEmpty() ? QStringLiteral("Account") : accountLabel_;
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
  return visibleTilesForWidth(viewportWidth);
}

bool YouTubeBrowsePage::hasInteractiveAuthCard() const {
  return authCard_ && authCard_->isVisible();
}

void YouTubeBrowsePage::updateSectionHeader() {
  if (titleLabel_) {
    titleLabel_->setText(QStringLiteral("YouTube"));
  }
  setWindowTitle(titleLabel_ ? titleLabel_->text() : QStringLiteral("YouTube"));
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
      iconLabel->setPixmap(guideIconPixmap(
          guideKey, QSize(kGuideIconGlyphSize, kGuideIconGlyphSize),
          iconColor));
      iconLabel->setProperty("aio_selected", selected);
      iconLabel->setProperty("aio_hovered", hovered);
      iconLabel->style()->unpolish(iconLabel);
      iconLabel->style()->polish(iconLabel);
      iconLabel->update();
    }
  }

  if (authCard_) {
    const bool selected = authCardSelected_;
    const bool hovered = inputMode_ == InputMode::Mouse && authCardHovered_;
    authCard_->setProperty(kTileSelectedProperty, selected);
    authCard_->setProperty(kTileHoveredProperty, hovered);
    authCard_->style()->unpolish(authCard_);
    authCard_->style()->polish(authCard_);
    authCard_->update();

    auto *shadow = ensureShadowEffect(authCard_);
    if (selected) {
      animateShadow(shadow, 28.0, QPointF(0.0, 10.0), QColor(255, 98, 90, 92),
                    170);
    } else if (hovered) {
      animateShadow(shadow, 18.0, QPointF(0.0, 8.0), QColor(255, 255, 255, 42),
                    140);
    } else {
      animateShadow(shadow, 0.0, QPointF(0.0, 0.0), Qt::transparent, 120);
    }
  }

  if (accountButton_) {
    const bool accountGuideActive =
        guideSelected_ && selectedGuideIndex_ >= 0 &&
        selectedGuideIndex_ < static_cast<int>(guideItems_.size()) &&
        (guideItems_[selectedGuideIndex_].key == QStringLiteral("account") ||
         guideItems_[selectedGuideIndex_].key == QStringLiteral("connect"));
    accountButton_->setProperty("aio_selected",
                                authCardSelected_ || accountGuideActive);
    accountButton_->style()->unpolish(accountButton_);
    accountButton_->style()->polish(accountButton_);
    accountButton_->update();
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
      const QRect baseRect = tile->property(kTileBaseRectProperty).toRect();
      const QRect targetRect = selected ? expandedRectFor(baseRect) : baseRect;
      tile->setProperty(kTileSelectedProperty, selected);
      tile->setProperty(kTileHoveredProperty, hovered);
      tile->style()->unpolish(tile);
      tile->style()->polish(tile);
      tile->update();
      tile->raise();

      animateRect(tile, targetRect, selected ? 170 : 140);

      auto *shadow = ensureShadowEffect(tile);
      if (selected) {
        animateShadow(shadow, 34.0, QPointF(0.0, 12.0),
                      QColor(255, 255, 255, 68), 170);
      } else if (hovered) {
        animateShadow(shadow, 20.0, QPointF(0.0, 8.0),
                      QColor(255, 255, 255, 36), 150);
      } else {
        animateShadow(shadow, 0.0, QPointF(0.0, 0.0), Qt::transparent, 120);
      }
    }
  }
}

void YouTubeBrowsePage::activateFocused() {
  if (guideSelected_) {
    activateGuideItem();
    return;
  }
  if (authCardSelected_) {
    performAuthPrimaryAction();
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
    if (hasInteractiveAuthCard()) {
      setFocusToAuthCard(true);
    } else {
      performAuthPrimaryAction();
    }
    return;
  }

  const int railIndex = railIndexForGuideSelection();
  if (railIndex >= 0 && railIndex < static_cast<int>(rails_.size()) &&
      !rails_[railIndex].items.empty()) {
    setFocusedItem(railIndex, effectiveItemIndexForRail(railIndex), true);
  }
}

void YouTubeBrowsePage::ensureFocusedVisible() {
  if (authCardSelected_) {
    return;
  }
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
      if (hasInteractiveAuthCard() && selectedGuideIndex_ >= 0 &&
          selectedGuideIndex_ < static_cast<int>(guideItems_.size())) {
        const QString guideKey = guideItems_[selectedGuideIndex_].key;
        if (guideKey == QStringLiteral("account") ||
            guideKey == QStringLiteral("connect")) {
          setFocusToAuthCard(true);
          return;
        }
      }
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

  if (authCardSelected_) {
    if (dx < 0 || dy < 0) {
      guideSelected_ = true;
      authCardSelected_ = false;
      updateSidebarState(true);
      updateSectionHeader();
      updateFocusStyle();
      updateHeroSpotlight();
      return;
    }
    if ((dx > 0 || dy > 0) && !rails_.empty() &&
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
      if (!searchEdit_->text().trimmed().isEmpty()) {
        searchEdit_->clear();
      } else {
        setSearchFocused(false);
      }
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

  const bool focusSearchShortcut =
      (((event->modifiers() & Qt::ControlModifier) ||
        (event->modifiers() & Qt::MetaModifier)) &&
       (event->key() == Qt::Key_F || event->key() == Qt::Key_L ||
        event->key() == Qt::Key_K)) ||
      event->key() == Qt::Key_Slash;
  if (focusSearchShortcut) {
    setSearchFocused(true);
    event->accept();
    return;
  }

  const QString typedText = event->text();
  const bool plainTextInput =
      typedText.size() == 1 && typedText.front().isPrint() &&
      !typedText.front().isSpace() &&
      !(event->modifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  if (plainTextInput) {
    setSearchFocused(true);
    if (searchEdit_) {
      searchEdit_->insert(typedText);
    }
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
  const int viewportWidth =
      scroll_ && scroll_->viewport() ? scroll_->viewport()->width() : width();
  if (viewportWidth > 0 &&
      tileWidthForViewport(viewportWidth) != contentTileWidth_) {
    scheduleContentRebuild();
    return;
  }
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

  if (isAuthObject(watched)) {
    if (event->type() == QEvent::Enter) {
      setInputMode(InputMode::Mouse);
      authCardHovered_ = true;
      authCardSelected_ = true;
      guideSelected_ = false;
      updateSidebarState(true);
      updateFocusStyle();
    } else if (event->type() == QEvent::Leave) {
      authCardHovered_ = false;
      updateFocusStyle();
    } else if (event->type() == QEvent::MouseButtonPress) {
      auto *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        setInputMode(InputMode::Mouse);
        authCardHovered_ = true;
        authCardSelected_ = true;
        guideSelected_ = false;
        updateSidebarState(true);
        updateFocusStyle();
        performAuthPrimaryAction();
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