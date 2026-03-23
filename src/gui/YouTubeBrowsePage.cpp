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
#include <QListWidget>
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
#include <QUrlQuery>
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
constexpr auto kThumbMissingProperty = "aio_thumb_missing";
constexpr int kCollapsedSidebarWidth = 72;
constexpr int kExpandedSidebarWidth = 240;
constexpr int kGuideTextWidth = 168;
constexpr int kGuideTextCollapsedWidth = 0;
constexpr int kGuideIconFrameSize = 36;
constexpr int kGuideIconGlyphSize = 20;
constexpr int kTileSlotPadding = 12;
constexpr int kTileFocusGrow = 12;
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
  if (viewportWidth >= 1500) {
    return 5;
  }
  if (viewportWidth >= 860) {
    return 4;
  }
  if (viewportWidth >= 620) {
    return 3;
  }
  if (viewportWidth >= 420) {
    return 2;
  }
  return 1;
}

int tileWidthForViewport(int viewportWidth) {
  const int visibleTiles = visibleTilesForWidth(viewportWidth);
  const int spacing = 12;
  const int availableWidth =
      std::max(360, viewportWidth - ((visibleTiles - 1) * spacing) - 32);
  return std::clamp(availableWidth / std::max(1, visibleTiles), 164, 300);
}

QString scrubDescription(QString text) {
  static const QRegularExpression reUrl(QStringLiteral("https?://\\S+"));
  static const QRegularExpression reDownload(
      QStringLiteral("(?i)download:?\\s*"));
  static const QRegularExpression reStream(QStringLiteral("(?i)stream/"));
  text.replace(reUrl, QString());
  text.replace(reDownload, QString());
  text.replace(reStream, QString());
  return text.simplified();
}

QString conciseMeta(const AIO::Streaming::VideoContent &item) {
  QStringList parts;

  const QString channel = QString::fromStdString(item.channelName).trimmed();
  if (!channel.isEmpty()) {
    parts << channel;
  }

  const QString views = QString::fromStdString(item.viewCount).trimmed();
  if (!views.isEmpty()) {
    parts << views;
  }

  const QString published = QString::fromStdString(item.publishedAt).trimmed();
  if (!published.isEmpty()) {
    parts << published;
  }

  if (!parts.isEmpty()) {
    return parts.join(QStringLiteral(" \u00B7 "));
  }

  // Fallback to old behavior
  const QString category = QString::fromStdString(item.category).simplified();
  if (!category.isEmpty()) {
    return category;
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

void setThumbMissingState(
    ThumbnailFillLabel *label, bool missing,
    const QString &fallbackText = QStringLiteral("YouTube")) {
  if (!label) {
    return;
  }

  label->setProperty(kThumbMissingProperty, missing);
  if (missing) {
    label->clearSourcePixmap();
    label->setText(fallbackText);
  }
  label->style()->unpolish(label);
  label->style()->polish(label);
  label->update();
}

bool applyThumbnailIfAvailable(ThumbnailFillLabel *label, const QString &url) {
  if (!label || url.isEmpty()) {
    return false;
  }

  QPixmap pixmap;
  if (!ThumbnailCache::instance().tryGet(url, &pixmap)) {
    return false;
  }

  label->setSourcePixmap(pixmap);
  setThumbMissingState(label, false);
  return true;
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
  result.setDevicePixelRatio(source.devicePixelRatio());

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
  const qreal dpr = qApp->devicePixelRatio();
  QPixmap pixmap(
      QSize(qRound(size.width() * dpr), qRound(size.height() * dpr)));
  pixmap.setDevicePixelRatio(dpr);
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
  } else if (key == QStringLiteral("back")) {
    painter.drawLine(QPointF(box.center().x() + 2.0, box.top() + 3.0),
                     QPointF(box.left() + 2.0, box.center().y()));
    painter.drawLine(QPointF(box.left() + 2.0, box.center().y()),
                     QPointF(box.center().x() + 2.0, box.bottom() - 3.0));
  } else {
    painter.drawEllipse(box.adjusted(2.0, 2.0, -2.0, -2.0));
  }

  return pixmap;
}

} // namespace

YouTubeBrowsePage::YouTubeBrowsePage(QWidget *parent) : QWidget(parent) {
  setObjectName("aioYouTubePage");
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
  sidebarLayout->setContentsMargins(10, 16, 10, 16);
  sidebarLayout->setSpacing(8);

  sidebarTitleLabel_ = new QLabel("YouTube", sidebar_);
  sidebarTitleLabel_->setObjectName("aioYouTubeSidebarBrand");
  sidebarTitleLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  sidebarTitleLabel_->setMinimumSize(48, 36);
  sidebarTitleLabel_->setMaximumSize(188, 44);
  sidebarTitleLabel_->setContentsMargins(16, 0, 16, 0);
  sidebarTitleLabel_->setProperty("expanded", false);
  sidebarLayout->addWidget(sidebarTitleLabel_, 0, Qt::AlignHCenter);

  guideList_ = new QWidget(sidebar_);
  guideLayout_ = new QVBoxLayout(guideList_);
  guideLayout_->setContentsMargins(0, 0, 0, 0);
  guideLayout_->setSpacing(4);
  sidebarLayout->addWidget(guideList_);
  sidebar_->installEventFilter(this);
  guideList_->installEventFilter(this);
  sidebarLayout->addStretch();

  auto *mainShell = new QWidget(this);
  mainShell->setObjectName("aioYouTubeMainShell");
  auto *mainLayout = new QVBoxLayout(mainShell);
  mainLayout->setContentsMargins(16, 12, 16, 16);
  mainLayout->setSpacing(10);

  topBar_ = new QWidget(mainShell);
  topBar_->setObjectName("aioTopBar");
  topBar_->installEventFilter(this);
  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(0, 4, 0, 4);
  barLayout->setSpacing(8);

  backButton_ = new QPushButton(topBar_);
  backButton_->setFocusPolicy(Qt::NoFocus);
  backButton_->setProperty("variant", "secondary");
  backButton_->setFixedSize(42, 42);
  backButton_->setToolTip(QStringLiteral("Back"));
  backButton_->setIcon(QIcon(guideIconPixmap(
      QStringLiteral("back"), QSize(kGuideIconGlyphSize, kGuideIconGlyphSize),
      QColor(QStringLiteral("#f1f1f1")))));
  backButton_->setIconSize(QSize(kGuideIconGlyphSize, kGuideIconGlyphSize));

  homeButton_ = new QPushButton(topBar_);
  homeButton_->setFocusPolicy(Qt::NoFocus);
  homeButton_->setProperty("variant", "secondary");
  homeButton_->setFixedSize(42, 42);
  homeButton_->setToolTip(QStringLiteral("Home"));
  homeButton_->setIcon(QIcon(guideIconPixmap(
      QStringLiteral("home"), QSize(kGuideIconGlyphSize, kGuideIconGlyphSize),
      QColor(QStringLiteral("#f1f1f1")))));
  homeButton_->setIconSize(QSize(kGuideIconGlyphSize, kGuideIconGlyphSize));

  titleLabel_ = new QLabel("YouTube", topBar_);
  titleLabel_->setProperty("role", "ytBrowseHeaderTitle");

  searchEdit_ = new QLineEdit(topBar_);
  searchEdit_->setPlaceholderText("Search YouTube");
  searchEdit_->setObjectName("aioYouTubeSearch");
  searchEdit_->setMinimumWidth(280);
  searchEdit_->setMaximumWidth(420);
  searchEdit_->setMinimumHeight(42);

  searchButton_ = new QPushButton("Search", topBar_);
  searchButton_->setFocusPolicy(Qt::NoFocus);
  searchButton_->setProperty("variant", "secondary");
  searchButton_->setObjectName("aioYouTubeSearchButton");
  searchButton_->setVisible(false);

  accountButton_ = new QPushButton(topBar_);
  accountButton_->setObjectName("aioYouTubeAccountButton");
  accountButton_->setFocusPolicy(Qt::NoFocus);
  accountButton_->setCursor(Qt::PointingHandCursor);
  accountButton_->setFixedSize(42, 42);
  accountButton_->setText("YT");
  accountButton_->setProperty("variant", "secondary");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(titleLabel_);
  barLayout->addStretch();
  barLayout->addWidget(searchEdit_);
  barLayout->addSpacing(4);
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
  heroCard_->setMinimumHeight(260);
  heroCard_->setMaximumHeight(460);

  auto *heroLayout = new QGridLayout(heroCard_);
  heroLayout->setContentsMargins(0, 0, 0, 0);
  heroLayout->setSpacing(0);

  heroArtworkLabel_ = new ThumbnailFillLabel(heroCard_);
  heroArtworkLabel_->setObjectName("aioYouTubeHeroArtwork");
  heroArtworkLabel_->setText(QStringLiteral("Loading"));
  heroArtworkLabel_->setAlignment(Qt::AlignCenter);

  auto *heroScrim = new QWidget(heroCard_);
  heroScrim->setObjectName("aioYouTubeHeroScrim");

  auto *heroCopyWrap = new QWidget(heroCard_);
  heroCopyWrap->setObjectName("aioYouTubeHeroCopy");
  auto *heroCopyColumn = new QVBoxLayout(heroCopyWrap);
  heroCopyColumn->setContentsMargins(24, 24, 24, 24);
  heroCopyColumn->setSpacing(8);

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
  heroChipRow->setContentsMargins(0, 2, 0, 0);
  heroChipRow->setSpacing(8);
  heroPrimaryChip_ = makeHeroChip(QString());
  heroSecondaryChip_ = makeHeroChip(QString());
  heroTertiaryChip_ = makeHeroChip(QString());
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
  heroSpotlightLayout->setContentsMargins(16, 16, 16, 16);
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
  heroSpotlightCard_->setVisible(false);

  heroLayout->addWidget(heroArtworkLabel_, 0, 0);
  heroLayout->addWidget(heroScrim, 0, 0);
  heroLayout->addWidget(heroCopyWrap, 0, 0);
  heroLayout->addWidget(heroSpotlightCard_, 0, 0,
                        Qt::AlignRight | Qt::AlignTop);

  authCard_ = new QFrame(heroRow);
  authCard_->setObjectName("aioYouTubeAccountStrip");
  authCard_->setProperty(kAuthCardProperty, true);
  authCard_->setFocusPolicy(Qt::NoFocus);
  authCard_->setMinimumWidth(320);
  authCard_->setMaximumWidth(380);
  authCard_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  authCard_->installEventFilter(this);

  auto *authLayout = new QVBoxLayout(authCard_);
  authLayout->setContentsMargins(22, 18, 22, 18);
  authLayout->setSpacing(12);

  auto *authTopRow = new QHBoxLayout();
  authTopRow->setSpacing(22);

  auto *authCopyColumn = new QVBoxLayout();
  authCopyColumn->setContentsMargins(0, 0, 0, 0);
  authCopyColumn->setSpacing(8);

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
  identityRow->setSpacing(14);
  identityRow->addWidget(authAvatarLabel_, 0, Qt::AlignTop);

  auto *identityText = new QVBoxLayout();
  identityText->setContentsMargins(0, 0, 0, 0);
  identityText->setSpacing(4);
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

  heroRowLayout->addWidget(heroCard_, 1);

  authCard_->setParent(mainShell);
  authCard_->setMinimumWidth(0);
  authCard_->setMaximumWidth(QWIDGETSIZE_MAX);
  authCard_->setMinimumHeight(132);
  authCard_->setMaximumHeight(184);
  authCard_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  auto *contentWrapper = new QWidget(mainShell);
  contentWrapper->setObjectName("aioYouTubeContentShell");
  auto *contentLayout = new QVBoxLayout(contentWrapper);
  contentLayout->setContentsMargins(0, 12, 0, 0);
  contentLayout->setSpacing(16);

  scroll_ = new QScrollArea(contentWrapper);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->installEventFilter(this);

  contentHost_ = new QWidget(scroll_);
  contentLayout_ = new QVBoxLayout(contentHost_);
  contentLayout_->setContentsMargins(0, 12, 0, 48);
  contentLayout_->setSpacing(28);

  scroll_->setWidget(contentHost_);
  contentLayout->addWidget(scroll_, 1);

  mainLayout->addWidget(topBar_);

  // ── Category filter strip ─────────────────────────────────────────────
  categoryScrollArea_ = new QScrollArea(mainShell);
  categoryScrollArea_->setObjectName("aioYouTubeCategoryScroll");
  categoryScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  categoryScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  categoryScrollArea_->setFrameShape(QFrame::NoFrame);
  categoryScrollArea_->setFixedHeight(62);
  categoryScrollArea_->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Fixed);

  categoryStrip_ = new QWidget();
  categoryStrip_->setObjectName("aioYouTubeCategoryStrip");
  auto *chipLayout = new QHBoxLayout(categoryStrip_);
  chipLayout->setContentsMargins(4, 6, 4, 6);
  chipLayout->setSpacing(10);

  const QStringList categories = {
      "All",  "Gaming",   "Music",      "Live",    "Sports",
      "News", "Learning", "Technology", "Cooking", "Podcasts"};

  for (const QString &cat : categories) {
    auto *chip = new QPushButton(cat, categoryStrip_);
    chip->setObjectName("aioYouTubeCategoryChip");
    chip->setFocusPolicy(Qt::NoFocus);
    chip->setCheckable(true);
    if (cat == "All") {
      chip->setChecked(true);
      chip->setProperty("active", true);
      chip->setProperty("aio_selected", true);
    } else {
      chip->setProperty("aio_selected", false);
    }
    connect(chip, &QPushButton::clicked, this, [this, cat, chip]() {
      if (!categoryStrip_) {
        return;
      }
      const auto allChips = categoryStrip_->findChildren<QPushButton *>();
      for (auto *c : allChips) {
        c->setChecked(false);
        c->setProperty("active", false);
        c->setProperty("aio_selected", false);
        c->style()->unpolish(c);
        c->style()->polish(c);
      }
      chip->setChecked(true);
      chip->setProperty("active", true);
      chip->setProperty("aio_selected", true);
      chip->style()->unpolish(chip);
      chip->style()->polish(chip);
      activeCategory_ = (cat == "All") ? QString() : cat;
      if (activeCategory_.isEmpty()) {
        loadTrending();
      } else {
        setLoadingState(true, QStringLiteral("Loading %1\u2026").arg(cat));
        const uint64_t requestId = ++requestSerial_;
        QPointer<YouTubeBrowsePage> guard(this);
        auto *service = youTube_;
        const std::string catQuery = cat.toStdString();
        std::thread([guard, requestId, service, catQuery, cat]() {
          std::vector<AIO::Streaming::YouTubeContentRail> rails;
          if (service) {
            auto results = service->search(catQuery, 20);
            AIO::Streaming::YouTubeContentRail rail;
            rail.key = catQuery;
            rail.title = catQuery;
            rail.items = std::move(results);
            rails.push_back(std::move(rail));
          }
          QMetaObject::invokeMethod(
              guard.data(),
              [guard, requestId, rails = std::move(rails), cat]() mutable {
                if (!guard || requestId != guard->requestSerial_) {
                  return;
                }
                guard->setRails(rails, {}, {});
                guard->setLoadingState(
                    false, rails.empty()
                               ? QStringLiteral("No results for %1").arg(cat)
                               : QStringLiteral("%1 videos loaded")
                                     .arg(static_cast<int>(
                                         rails.front().items.size())));
              },
              Qt::QueuedConnection);
        }).detach();
      }
    });
    chipLayout->addWidget(chip);
  }
  chipLayout->addStretch();
  categoryScrollArea_->setWidget(categoryStrip_);
  categoryScrollArea_->setWidgetResizable(true);
  mainLayout->addWidget(categoryScrollArea_);

  mainLayout->addWidget(heroRow);
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

  hoverPreviewTimer_ = new QTimer(this);
  hoverPreviewTimer_->setSingleShot(true);
  hoverPreviewTimer_->setInterval(10000);
  connect(hoverPreviewTimer_, &QTimer::timeout, this,
          [this]() { prefetchFocusedStream(); });

  streamPrefetchTimer_ = new QTimer(this);
  streamPrefetchTimer_->setSingleShot(true);
  streamPrefetchTimer_->setInterval(2500);
  connect(streamPrefetchTimer_, &QTimer::timeout, this,
          &YouTubeBrowsePage::prefetchFocusedStream);

  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &url) {
            refreshLoadedThumbnail(url);
            refreshAuthArtwork(url);
          });

  // ── Search suggestion dropdown ────────────────────────────────────────
  suggestionDropdown_ = new QFrame(this);
  suggestionDropdown_->setObjectName("aioYouTubeSuggestionDropdown");
  suggestionDropdown_->setFrameShape(QFrame::NoFrame);
  suggestionDropdown_->hide();

  auto *dropLayout = new QVBoxLayout(suggestionDropdown_);
  dropLayout->setContentsMargins(0, 4, 0, 0);
  dropLayout->setSpacing(0);

  suggestionList_ = new QListWidget(suggestionDropdown_);
  suggestionList_->setObjectName("aioYouTubeSuggestionList");
  suggestionList_->setFocusPolicy(Qt::NoFocus);
  suggestionList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  suggestionList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  dropLayout->addWidget(suggestionList_);

  suggestionDebounceTimer_ = new QTimer(this);
  suggestionDebounceTimer_->setSingleShot(true);
  suggestionDebounceTimer_->setInterval(320);

  connect(searchEdit_, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            if (!suggestionDropdown_ || !suggestionDebounceTimer_) {
              return;
            }
            if (text.trimmed().isEmpty()) {
              suggestionDropdown_->hide();
              return;
            }
            suggestionDebounceTimer_->stop();
            suggestionDebounceTimer_->start();
          });

  connect(suggestionDebounceTimer_, &QTimer::timeout, this, [this]() {
    if (!searchEdit_ || !youTube_) {
      return;
    }
    const QString query = searchEdit_->text().trimmed();
    if (query.isEmpty()) {
      return;
    }
    youTube_->fetchSearchSuggestionsAsync(
        query.toStdString(), [this](std::vector<std::string> suggestions) {
          QMetaObject::invokeMethod(
              this,
              [this, suggestions = std::move(suggestions)]() {
                if (!suggestionList_ || !suggestionDropdown_) {
                  return;
                }
                suggestionList_->clear();
                for (const auto &s : suggestions) {
                  suggestionList_->addItem(QString::fromStdString(s));
                }
                if (!suggestions.empty()) {
                  positionSuggestionDropdown();
                  suggestionDropdown_->show();
                  suggestionDropdown_->raise();
                }
              },
              Qt::QueuedConnection);
        });
  });

  connect(suggestionList_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem *item) {
            if (!item || !searchEdit_ || !suggestionDropdown_) {
              return;
            }
            searchEdit_->setText(item->text());
            suggestionDropdown_->hide();
            runSearch();
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

  if (heroArtworkLabel_ &&
      heroArtworkLabel_->property(kThumbnailUrlProperty).toString() == url) {
    applyThumbnailIfAvailable(heroArtworkLabel_, url);
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

void YouTubeBrowsePage::showSkeletonRails() {
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

  for (int rail = 0; rail < 3; ++rail) {
    auto *railBlock = new QWidget(contentHost_);
    auto *railLayout = new QVBoxLayout(railBlock);
    railLayout->setContentsMargins(12, rail == 0 ? 20 : 8, 12, 14);
    railLayout->setSpacing(12);

    auto *titleBar = new QLabel(railBlock);
    titleBar->setObjectName(QStringLiteral("aioSkeletonTextBar"));
    titleBar->setFixedSize(160, 18);
    railLayout->addWidget(titleBar);

    auto *tileRow = new QWidget(railBlock);
    auto *tileRowLayout = new QHBoxLayout(tileRow);
    tileRowLayout->setContentsMargins(0, 6, 10, 6);
    tileRowLayout->setSpacing(14);

    for (int t = 0; t < 5; ++t) {
      auto *tile = new QFrame(tileRow);
      tile->setObjectName(QStringLiteral("aioSkeletonTile"));
      tile->setFixedSize(tileWidth, thumbHeight + 64);

      auto *tileLayout = new QVBoxLayout(tile);
      tileLayout->setContentsMargins(0, 0, 0, 8);
      tileLayout->setSpacing(8);

      auto *thumb = new QLabel(tile);
      thumb->setObjectName(QStringLiteral("aioSkeletonThumb"));
      thumb->setFixedHeight(thumbHeight);
      thumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

      auto *textBar1 = new QLabel(tile);
      textBar1->setObjectName(QStringLiteral("aioSkeletonTextBar"));
      textBar1->setFixedSize(tileWidth - 16, 14);

      auto *textBar2 = new QLabel(tile);
      textBar2->setObjectName(QStringLiteral("aioSkeletonTextBar"));
      textBar2->setFixedSize((tileWidth - 16) * 2 / 3, 12);

      tileLayout->addWidget(thumb);
      tileLayout->addWidget(textBar1);
      tileLayout->addWidget(textBar2);

      tileRowLayout->addWidget(tile);
    }

    railLayout->addWidget(tileRow);
    contentLayout_->addWidget(railBlock);
  }
  contentLayout_->addStretch();
}

void YouTubeBrowsePage::refreshHome() {
  if (!youTube_) {
    setStatus("YouTube service not available.");
    return;
  }

  setLoadingState(true, "Loading YouTube home...");
  showSkeletonRails();
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
  if (suggestionDropdown_) {
    suggestionDropdown_->hide();
  }
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
  showSkeletonRails();
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

  heroCarouselIndex_ = 0;
  cancelHoverTimers();

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
  guideSelected_ = rails_.empty();
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
  } else if (!rails_.empty()) {
    selectedGuideIndex_ = 0;
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
    textClip->setMaximumWidth(guideSelected_ ? kGuideTextWidth
                                             : kGuideTextCollapsedWidth);
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

  if (!signedIn && !active) {
    authCard_->hide();

    authAvatarLabel_->setPixmap(QPixmap());
    authAvatarLabel_->setText(accountLabel_.isEmpty()
                                  ? QStringLiteral("YT")
                                  : accountLabel_.left(1).toUpper());
    authTitleLabel_->setText(
        canStartDeviceAuth ? QStringLiteral("Sign in for subscriptions")
                           : QStringLiteral("Sign-in server unavailable"));
    authBodyLabel_->setText(
        canStartDeviceAuth
            ? QStringLiteral(
                  "Unlock subscriptions, watch later, likes, and synced watch "
                  "progress without leaving the TV flow.")
            : QStringLiteral("Start the YouTube auth server to enable account "
                             "connection and personalized rows."));
    authHintLabel_->setText(
        canStartDeviceAuth
            ? QStringLiteral("Use the profile button above or open Account "
                             "from the guide.")
            : QStringLiteral(
                  "Public browse rows remain available while signed out."));
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText(canStartDeviceAuth ? QStringLiteral("Sign in")
                                                   : QStringLiteral("Refresh"));
    authSecondaryButton_->setText(QString());
    authCodeLabel_->setVisible(false);
    authUrlLabel_->setVisible(false);
    authSecondaryButton_->setVisible(false);
    authQrLabel_->setVisible(false);
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
    authBodyLabel_->setVisible(true);
    updateFocusStyle();
    return;
  }

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
        "Approve this device in your browser to unlock subscriptions and "
        "watch progress.");
    authCodeLabel_->setText(
        authSession_->userCode.empty()
            ? QString()
            : QStringLiteral("Code: %1")
                  .arg(QString::fromStdString(authSession_->userCode)));
    authUrlLabel_->setText(QString());
    authHintLabel_->setText(
        QStringLiteral("This page refreshes automatically after approval."));
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Open on Phone");
    authSecondaryButton_->setText("Cancel");
    authCodeLabel_->setVisible(true);
    authUrlLabel_->setVisible(false);
    authSecondaryButton_->setVisible(true);
    authQrLabel_->setVisible(false);
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
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
        "Unlock subscriptions, watch later, likes, and synced watch progress "
        "without leaving the TV flow.");
    authHintLabel_->setText(
        "Use the profile button above or select Sign in here.");
    authFooterLabel_->setText(QString());
    authPrimaryButton_->setText("Sign in");
    authHintLabel_->setVisible(true);
    authFooterLabel_->setVisible(false);
    authBodyLabel_->setVisible(true);
  }

  updateFocusStyle();
}

void YouTubeBrowsePage::advanceHeroCarousel() {
  if (rails_.empty() || rails_.front().items.empty()) {
    return;
  }
  heroCarouselIndex_ =
      (heroCarouselIndex_ + 1) % static_cast<int>(rails_.front().items.size());
  updateHeroSpotlight();
}

void YouTubeBrowsePage::startHoverTimers() {
  if (hoverPreviewTimer_) {
    hoverPreviewTimer_->start();
  }
  if (streamPrefetchTimer_) {
    streamPrefetchTimer_->start();
  }
}

void YouTubeBrowsePage::cancelHoverTimers() {
  if (hoverPreviewTimer_) {
    hoverPreviewTimer_->stop();
  }
  if (streamPrefetchTimer_) {
    streamPrefetchTimer_->stop();
  }
}

void YouTubeBrowsePage::prefetchFocusedStream() {
  if (!youTube_ || guideSelected_ || focusedRailIndex_ < 0 ||
      focusedRailIndex_ >= static_cast<int>(rails_.size()) ||
      focusedItemIndex_ < 0 ||
      focusedItemIndex_ >=
          static_cast<int>(rails_[focusedRailIndex_].items.size())) {
    return;
  }
  const auto &item = rails_[focusedRailIndex_].items[focusedItemIndex_];
  std::string id = item.id;
  if (id.empty()) {
    const QUrl parsed(QString::fromStdString(item.videoUrl));
    const QUrlQuery q(parsed);
    id = q.queryItemValue(QStringLiteral("v")).toStdString();
  }
  if (id.empty()) {
    return;
  }
  auto *service = youTube_;
  std::thread([service, id]() { service->resolvePlaybackStream(id); }).detach();
}

void YouTubeBrowsePage::updateHeroSpotlight() {
  if (!heroEyebrowLabel_ || !heroTitleLabel_ || !heroBodyLabel_ ||
      !heroPrimaryChip_ || !heroSecondaryChip_ || !heroTertiaryChip_ ||
      !heroSpotlightEyebrowLabel_ || !heroSpotlightTitleLabel_ ||
      !heroSpotlightMetaLabel_ || !heroArtworkLabel_) {
    return;
  }

  const bool signedIn = youTube_ && youTube_->hasOAuthAccess();
  const bool accountContext =
      authCardSelected_ ||
      (guideSelected_ && selectedGuideIndex_ >= 0 &&
       selectedGuideIndex_ < static_cast<int>(guideItems_.size()) &&
       (guideItems_[selectedGuideIndex_].key == QStringLiteral("account") ||
        guideItems_[selectedGuideIndex_].key == QStringLiteral("connect")));

  const AIO::Streaming::VideoContent *heroItem = nullptr;
  QString eyebrow;
  if (guideSelected_ && selectedGuideIndex_ >= 0 &&
      selectedGuideIndex_ < static_cast<int>(guideItems_.size())) {
    const QString guideTitle = guideItems_[selectedGuideIndex_].title.toUpper();
    eyebrow = guideTitle.isEmpty() ? QStringLiteral("FEATURED") : guideTitle;
  } else if (focusedRailIndex_ >= 0 &&
             focusedRailIndex_ < static_cast<int>(rails_.size())) {
    eyebrow = rails_[focusedRailIndex_].title.toUpper();
    if (eyebrow.isEmpty()) {
      eyebrow = QStringLiteral("FEATURED");
    }
  } else {
    eyebrow = QStringLiteral("FEATURED");
  }
  QString title = currentSectionTitle_.trimmed();
  QString body;
  QString spotlightEyebrow = QStringLiteral("ACCOUNT");
  QString spotlightTitle;
  QString spotlightMeta;
  bool showSpotlightCard = false;

  if (!guideSelected_ && focusedRailIndex_ >= 0 &&
      focusedRailIndex_ < static_cast<int>(rails_.size()) &&
      focusedItemIndex_ >= 0 &&
      focusedItemIndex_ <
          static_cast<int>(rails_[focusedRailIndex_].items.size())) {
    heroItem = &rails_[focusedRailIndex_].items[focusedItemIndex_];
  } else if (!rails_.empty() && !rails_.front().items.empty()) {
    const int idx =
        heroCarouselIndex_ % static_cast<int>(rails_.front().items.size());
    heroItem = &rails_.front().items[idx];
  }

  if (heroItem) {
    const QString itemTitle =
        QString::fromStdString(heroItem->title).simplified();
    const QString itemMeta = summaryFor(*heroItem);
    const QString category =
        QString::fromStdString(heroItem->category).trimmed();
    const QString durationText = formatDuration(heroItem->durationSeconds);
    title = itemTitle.isEmpty() ? QStringLiteral("Featured video") : itemTitle;
    body = itemMeta.isEmpty() ? QStringLiteral("Play now")
                              : elideText(itemMeta, 120);

    heroPrimaryChip_->setText(QStringLiteral("Play now"));
    heroPrimaryChip_->setVisible(true);
    heroPrimaryChip_->setProperty("active", true);

    heroSecondaryChip_->setText(durationText);
    heroSecondaryChip_->setVisible(!durationText.isEmpty());
    heroSecondaryChip_->setProperty("active", false);

    heroTertiaryChip_->setText(category);
    heroTertiaryChip_->setVisible(!category.isEmpty());
    heroTertiaryChip_->setProperty("active", false);

    const QString thumbnailUrl =
        QString::fromStdString(heroItem->thumbnailUrl).trimmed();
    heroArtworkLabel_->setProperty(kThumbnailUrlProperty, thumbnailUrl);
    if (thumbnailUrl.isEmpty()) {
      heroArtworkLabel_->clearSourcePixmap();
      heroArtworkLabel_->setText(QStringLiteral("YouTube"));
    } else if (ThumbnailCache::instance().tryGet(thumbnailUrl, nullptr)) {
      applyThumbnailIfAvailable(heroArtworkLabel_, thumbnailUrl);
    } else {
      heroArtworkLabel_->clearSourcePixmap();
      heroArtworkLabel_->setText(QStringLiteral("Loading"));
      ThumbnailCache::instance().request(thumbnailUrl);
    }
  } else {
    heroArtworkLabel_->clearSourcePixmap();
    heroArtworkLabel_->setText(QStringLiteral("YouTube"));
    heroArtworkLabel_->setProperty(kThumbnailUrlProperty, QString());
    heroPrimaryChip_->setVisible(false);
    heroSecondaryChip_->setVisible(false);
    heroTertiaryChip_->setVisible(false);
    title = QStringLiteral("Popular on YouTube");
    body =
        signedIn
            ? QStringLiteral("Continue watching and subscriptions are ready.")
            : QStringLiteral("Browse public picks instantly.");
  }

  if (accountContext) {
    spotlightTitle =
        authTitleLabel_ ? authTitleLabel_->text().trimmed() : QString();
    spotlightMeta =
        authHintLabel_ ? authHintLabel_->text().trimmed() : QString();
    showSpotlightCard = !spotlightTitle.isEmpty() || !spotlightMeta.isEmpty();
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
    chip->setVisible(!text.trimmed().isEmpty());
    chip->setProperty("active", active);
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    chip->update();
  };

  applyChip(heroPrimaryChip_, heroPrimaryChip_->text(),
            heroPrimaryChip_->isVisible());
  applyChip(heroSecondaryChip_, heroSecondaryChip_->text(),
            heroSecondaryChip_->isVisible());
  applyChip(heroTertiaryChip_, heroTertiaryChip_->text(),
            heroTertiaryChip_->isVisible());
  heroSpotlightEyebrowLabel_->setText(spotlightEyebrow);
  heroSpotlightTitleLabel_->setText(spotlightTitle);
  heroSpotlightMetaLabel_->setText(spotlightMeta);
  heroSpotlightCard_->setVisible(showSpotlightCard);
}

void YouTubeBrowsePage::updateSidebarState(bool animated) {
  if (!sidebar_) {
    return;
  }

  const bool shouldExpand = guideSelected_;
  sidebarExpanded_ = shouldExpand;
  const uint64_t animationEpoch = ++sidebarAnimationEpoch_;

  if (sidebarTitleLabel_) {
    sidebarTitleLabel_->setProperty("expanded", shouldExpand);
    sidebarTitleLabel_->setText(shouldExpand ? QStringLiteral("YouTube TV")
                                             : QStringLiteral("YT"));
    sidebarTitleLabel_->setAlignment(shouldExpand
                                         ? Qt::AlignVCenter | Qt::AlignLeft
                                         : Qt::AlignVCenter | Qt::AlignCenter);
    sidebarTitleLabel_->setMinimumWidth(shouldExpand ? 188 : 48);
    sidebarTitleLabel_->setMaximumWidth(shouldExpand ? 220 : 52);
    sidebarTitleLabel_->setMinimumHeight(shouldExpand ? 44 : 36);
    sidebarTitleLabel_->setMaximumHeight(shouldExpand ? 44 : 36);
    sidebarTitleLabel_->style()->unpolish(sidebarTitleLabel_);
    sidebarTitleLabel_->style()->polish(sidebarTitleLabel_);
    sidebarTitleLabel_->update();
  }

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

    const int targetTextWidth =
        shouldExpand ? kGuideTextWidth : kGuideTextCollapsedWidth;
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
  const int tileHeight = thumbHeight + 102;
  contentViewportWidth_ = viewportWidth;
  contentTileWidth_ = tileWidth;

  for (int railIndex = 0; railIndex < static_cast<int>(rails_.size());
       ++railIndex) {
    const auto &rail = rails_[railIndex];

    auto *railBlock = new QWidget(contentHost_);
    railBlock->setObjectName("aioRailBlock");
    railBlock->setProperty("hero_rail", railIndex == 0);
    auto *railLayout = new QVBoxLayout(railBlock);
    railLayout->setContentsMargins(12, railIndex == 0 ? 20 : 8, 12, 14);
    railLayout->setSpacing(12);

    auto *header = new QWidget(railBlock);
    header->setObjectName("aioRailHeader");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    auto *headerCopy = new QWidget(header);
    auto *headerCopyLayout = new QVBoxLayout(headerCopy);
    headerCopyLayout->setContentsMargins(0, 0, 0, 0);
    headerCopyLayout->setSpacing(4);

    auto *title = new QLabel(rail.title, headerCopy);
    title->setProperty("role", "ytSectionTitle");
    auto *subtitle = new QLabel(rail.subtitle, headerCopy);
    subtitle->setProperty("role", "ytSectionMeta");
    subtitle->setWordWrap(true);
    subtitle->setVisible(!rail.subtitle.isEmpty());

    auto *countChip =
        new QLabel(QStringLiteral("%1 videos").arg(rail.items.size()), header);
    countChip->setProperty("role", "ytRailCountChip");
    countChip->setAlignment(Qt::AlignCenter);
    countChip->setVisible(rail.items.size() > 0);

    headerCopyLayout->addWidget(title);
    headerCopyLayout->addWidget(subtitle);
    headerLayout->addWidget(headerCopy, 1);
    headerLayout->addWidget(countChip, 0, Qt::AlignTop);

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
    itemsLayout->setContentsMargins(0, 6, 10, 6);
    itemsLayout->setSpacing(14);

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
      thumb->setText(QString());
      thumb->setProperty(kThumbMissingProperty, false);
      thumb->setProperty(kRailIndexProperty, railIndex);
      thumb->setProperty(kItemIndexProperty, itemIndex);
      thumb->installEventFilter(this);

      const QString categoryText =
          QString::fromStdString(item.channelName).trimmed();
      const QString durationText = formatDuration(item.durationSeconds);

      auto *thumbOverlay = new QWidget(thumbFrame);
      thumbOverlay->setObjectName("aioTileThumbOverlay");
      auto *thumbOverlayLayout = new QHBoxLayout(thumbOverlay);
      thumbOverlayLayout->setContentsMargins(12, 0, 12, 12);
      thumbOverlayLayout->setSpacing(8);

      auto *categoryChip = new QLabel(thumbOverlay);
      categoryChip->setProperty("role", "tileBadge");
      categoryChip->setText(categoryText);
      categoryChip->setVisible(!categoryText.isEmpty());

      auto *durationChip = new QLabel(thumbOverlay);
      durationChip->setProperty("role", "tileDurationBadge");
      durationChip->setText(durationText);
      durationChip->setVisible(!durationText.isEmpty());

      thumbOverlayLayout->addWidget(categoryChip);
      thumbOverlayLayout->addStretch();
      thumbOverlayLayout->addWidget(durationChip);

      if (item.isLive) {
        auto *liveBadge = new QLabel(QStringLiteral("LIVE"), thumbOverlay);
        liveBadge->setProperty("role", "tileLiveBadge");
        thumbOverlayLayout->addWidget(liveBadge);
        durationChip->setVisible(false);
      }

      thumbFrameLayout->addWidget(thumb, 0, 0);
      thumbFrameLayout->addWidget(thumbOverlay, 0, 0,
                                  Qt::AlignLeft | Qt::AlignBottom);

      // Watch-progress bar overlay
      if (item.watchProgressSeconds > 0 && item.durationSeconds > 0) {
        const float ratio =
            std::clamp(static_cast<float>(item.watchProgressSeconds) /
                           static_cast<float>(item.durationSeconds),
                       0.0f, 1.0f);
        constexpr int progressH = 3;
        auto *progressTrack = new QFrame(thumbFrame);
        progressTrack->setObjectName("aioTileProgressTrack");
        progressTrack->setGeometry(0, thumbHeight - progressH, tileWidth,
                                   progressH);
        progressTrack->raise();
        auto *progressFill = new QFrame(progressTrack);
        progressFill->setObjectName("aioTileProgressFill");
        progressFill->setGeometry(0, 0, static_cast<int>(tileWidth * ratio),
                                  progressH);
      }

      auto *copyWrap = new QWidget(tile);
      copyWrap->setObjectName("aioTileCopy");
      auto *copyLayout = new QVBoxLayout(copyWrap);
      copyLayout->setContentsMargins(10, 10, 10, 12);
      copyLayout->setSpacing(6);

      auto *itemTitle =
          new QLabel(elideText(QString::fromStdString(item.title), 68), tile);
      itemTitle->setProperty("role", "tileTitle");
      itemTitle->setWordWrap(true);
      itemTitle->setMaximumHeight(50);
      itemTitle->setProperty(kRailIndexProperty, railIndex);
      itemTitle->setProperty(kItemIndexProperty, itemIndex);
      itemTitle->installEventFilter(this);

      auto *meta = new QLabel(elideText(summaryFor(item), 60), tile);
      meta->setProperty("role", "tileMeta");
      meta->setWordWrap(true);
      meta->setMaximumHeight(34);
      meta->setVisible(!meta->text().trimmed().isEmpty());
      meta->setProperty(kRailIndexProperty, railIndex);
      meta->setProperty(kItemIndexProperty, itemIndex);
      meta->installEventFilter(this);

      const QString thumbUrl = QString::fromStdString(item.thumbnailUrl);
      thumb->setProperty(kThumbnailUrlProperty, thumbUrl);
      if (thumbUrl.isEmpty()) {
        setThumbMissingState(thumb, true);
      } else {
        setThumbMissingState(thumb, false);
        if (!applyThumbnailIfAvailable(thumb, thumbUrl)) {
          thumb->clearSourcePixmap();
          thumb->setText(QString());
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

    if (rail.items.size() >= 4) {
      auto *seeAllSlot = new QWidget(railHost);
      seeAllSlot->setFixedSize(tileWidth / 2, tileHeight);
      auto *seeAllCard = new QFrame(seeAllSlot);
      seeAllCard->setObjectName("aioSeeAllCard");
      seeAllCard->setGeometry(kTileSlotPadding, kTileSlotPadding,
                              (tileWidth / 2) - (kTileSlotPadding * 2),
                              tileHeight - (kTileSlotPadding * 2));
      auto *seeAllLayout = new QVBoxLayout(seeAllCard);
      seeAllLayout->setContentsMargins(12, 12, 12, 12);
      seeAllLayout->setAlignment(Qt::AlignCenter);
      auto *seeAllLabel =
          new QLabel(QStringLiteral("See all \u2192"), seeAllCard);
      seeAllLabel->setProperty("role", "seeAllText");
      seeAllLabel->setAlignment(Qt::AlignCenter);
      seeAllLabel->setWordWrap(true);
      seeAllLayout->addWidget(seeAllLabel);
      itemsLayout->addWidget(seeAllSlot, 0, Qt::AlignTop);
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

  cancelHoverTimers();

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
  startHoverTimers();
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
    if (libraryHeader_->property("aio_expanded").toBool() !=
        librarySectionExpanded_) {
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
    const bool activePage =
        !guideSelected_ && guideIndex == selectedGuideIndex_;

    const bool changed =
        button->property("aio_selected").toBool() != selected ||
        button->property("aio_hovered").toBool() != hovered ||
        button->property("aio_active_page").toBool() != activePage;
    if (!changed) {
      continue;
    }

    button->setProperty("aio_selected", selected);
    button->setProperty("aio_hovered", hovered);
    button->setProperty("aio_active_page", activePage);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();

    if (auto *iconLabel =
            button->findChild<QLabel *>("aioYouTubeGuideIconWrap")) {
      QColor iconColor = QColor(QStringLiteral("#f1f1f1"));
      if (selected) {
        iconColor = QColor(QStringLiteral("#111111"));
      } else if (activePage) {
        iconColor = QColor(QStringLiteral("#ffffff"));
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
      iconLabel->setProperty("aio_active_page", activePage);
      iconLabel->style()->unpolish(iconLabel);
      iconLabel->style()->polish(iconLabel);
      iconLabel->update();
    }
  }

  if (authCard_) {
    const bool selected = authCardSelected_;
    const bool hovered = inputMode_ == InputMode::Mouse && authCardHovered_;
    const bool authChanged =
        authCard_->property(kTileSelectedProperty).toBool() != selected ||
        authCard_->property(kTileHoveredProperty).toBool() != hovered;
    if (authChanged) {
      authCard_->setProperty(kTileSelectedProperty, selected);
      authCard_->setProperty(kTileHoveredProperty, hovered);
      authCard_->style()->unpolish(authCard_);
      authCard_->style()->polish(authCard_);
      authCard_->update();

      auto *shadow = ensureShadowEffect(authCard_);
      if (selected) {
        animateShadow(shadow, 34.0, QPointF(0.0, 12.0),
                      QColor(255, 108, 96, 104), 170);
      } else if (hovered) {
        animateShadow(shadow, 20.0, QPointF(0.0, 8.0),
                      QColor(255, 255, 255, 38), 140);
      } else {
        animateShadow(shadow, 0.0, QPointF(0.0, 0.0), Qt::transparent, 120);
      }
    }
  }

  if (accountButton_) {
    const bool accountGuideActive =
        guideSelected_ && selectedGuideIndex_ >= 0 &&
        selectedGuideIndex_ < static_cast<int>(guideItems_.size()) &&
        (guideItems_[selectedGuideIndex_].key == QStringLiteral("account") ||
         guideItems_[selectedGuideIndex_].key == QStringLiteral("connect"));
    const bool accountSelected = authCardSelected_ || accountGuideActive;
    if (accountButton_->property("aio_selected").toBool() != accountSelected) {
      accountButton_->setProperty("aio_selected", accountSelected);
      accountButton_->style()->unpolish(accountButton_);
      accountButton_->style()->polish(accountButton_);
      accountButton_->update();
    }
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

      const bool tileChanged =
          tile->property(kTileSelectedProperty).toBool() != selected ||
          tile->property(kTileHoveredProperty).toBool() != hovered;
      if (!tileChanged) {
        continue;
      }

      const QRect baseRect = tile->property(kTileBaseRectProperty).toRect();
      const QRect targetRect = selected ? expandedRectFor(baseRect) : baseRect;
      tile->setProperty(kTileSelectedProperty, selected);
      tile->setProperty(kTileHoveredProperty, hovered);
      tile->style()->unpolish(tile);
      tile->style()->polish(tile);
      tile->update();
      if (selected) {
        tile->raise();
      }

      animateRect(tile, targetRect, selected ? 170 : 140);

      auto *shadow = ensureShadowEffect(tile);
      if (selected) {
        animateShadow(shadow, 36.0, QPointF(0.0, 10.0),
                      QColor(255, 255, 255, 120), 170);
      } else if (hovered) {
        animateShadow(shadow, 22.0, QPointF(0.0, 10.0),
                      QColor(255, 255, 255, 32), 150);
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
    auto *targetTile = railTiles_[focusedRailIndex_][focusedItemIndex_];
    auto *hBar = railScrolls_[focusedRailIndex_]->horizontalScrollBar();
    if (hBar && targetTile) {
      const int targetX =
          targetTile->parentWidget()
              ? targetTile->parentWidget()
                    ->mapTo(railScrolls_[focusedRailIndex_]->widget(),
                            targetTile->parentWidget()->pos())
                    .x()
              : 0;
      const int visibleWidth =
          railScrolls_[focusedRailIndex_]->viewport()->width();
      int targetValue = targetX - visibleWidth / 4;
      targetValue = std::clamp(targetValue, hBar->minimum(), hBar->maximum());

      auto *hAnim = new QPropertyAnimation(hBar, "value", hBar);
      hAnim->setDuration(200);
      hAnim->setStartValue(hBar->value());
      hAnim->setEndValue(targetValue);
      hAnim->setEasingCurve(QEasingCurve::OutCubic);
      hAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
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
    if (dx != 0 && !rails_.empty() && !rails_.front().items.empty()) {
      const int count = static_cast<int>(rails_.front().items.size());
      heroCarouselIndex_ = (heroCarouselIndex_ + dx + count) % count;
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
    if (event->key() == Qt::Key_Down && suggestionDropdown_ &&
        suggestionDropdown_->isVisible() && suggestionList_ &&
        suggestionList_->count() > 0) {
      suggestionList_->setCurrentRow(0);
      suggestionList_->setFocus();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Escape) {
      if (suggestionDropdown_ && suggestionDropdown_->isVisible()) {
        suggestionDropdown_->hide();
        event->accept();
        return;
      }
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
    if (sidebarExpanded_) {
      guideSelected_ = false;
      updateSidebarState(true);
      event->accept();
      return;
    }
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
  const int heroH = qMax(260, height() / 4);
  if (heroCard_) {
    heroCard_->setFixedHeight(heroH);
  }
  if (suggestionDropdown_ && suggestionDropdown_->isVisible()) {
    positionSuggestionDropdown();
  }
  const int viewportWidth =
      scroll_ && scroll_->viewport() ? scroll_->viewport()->width() : width();
  if (viewportWidth > 0 &&
      tileWidthForViewport(viewportWidth) != contentTileWidth_) {
    scheduleContentRebuild();
    return;
  }
  ensureFocusedVisible();
}

void YouTubeBrowsePage::positionSuggestionDropdown() {
  if (!suggestionDropdown_ || !searchEdit_) {
    return;
  }
  const QPoint pos =
      searchEdit_->mapTo(this, QPoint(0, searchEdit_->height() + 2));
  const int dropWidth = qMax(searchEdit_->width(), 280);
  const int maxItems = qMin(6, suggestionList_ ? suggestionList_->count() : 0);
  const int itemH = 46;
  suggestionDropdown_->setGeometry(pos.x(), pos.y(), dropWidth,
                                   maxItems * itemH + 8);
  if (suggestionList_) {
    suggestionList_->setFixedHeight(maxItems * itemH);
  }
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