#include "gui/youtube/YouTubePlayerOverlay.h"

#include "gui/ThumbnailCache.h"
#include "gui/ThumbnailFillLabel.h"

#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr auto kThumbnailUrlProperty = "aio_thumbnail_url";

QString formatDurationLabel(int seconds) {
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

QString formatTime(int totalSeconds) {
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10,
                                                  QLatin1Char('0'));
}

QString compactSummary(const AIO::Streaming::VideoContent &item) {
  QString description = QString::fromStdString(item.description);
  description.replace(QRegularExpression(QStringLiteral("https?://\\S+")),
                      QString());
  description = description.simplified();
  if (!description.isEmpty()) {
    if (description.size() > 44) {
      return description.left(43) + QChar(0x2026);
    }
    return description;
  }

  const QString category = QString::fromStdString(item.category).trimmed();
  if (!category.isEmpty()) {
    return QStringLiteral("%1 pick").arg(category);
  }
  return QStringLiteral("Play next");
}

} // namespace

namespace AIO {
namespace GUI {

YouTubePlayerOverlay::YouTubePlayerOverlay(QWidget *parent) : QFrame(parent) {
  setObjectName("aioYouTubePlayerOverlay");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);

  auto *overlayLayout = new QVBoxLayout(this);
  overlayLayout->setContentsMargins(28, 24, 28, 26);
  overlayLayout->setSpacing(16);

  titleLabel_ = new QLabel("Now playing", this);
  titleLabel_->setProperty("role", "ytSectionTitle");

  hintLabel_ = new QLabel(this);
  hintLabel_->setProperty("role", "ytSectionMeta");
  hintLabel_->setWordWrap(true);

  transportBar_ = new QFrame(this);
  transportBar_->setObjectName("aioYouTubeTransportBar");
  auto *transportLayout = new QHBoxLayout(transportBar_);
  transportLayout->setContentsMargins(14, 12, 14, 12);
  transportLayout->setSpacing(10);

  auto makeChip = [&](const QString &text, const QString &name) {
    auto *chip = new QLabel(text, transportBar_);
    chip->setObjectName(name);
    chip->setProperty("role", "ytTransportChip");
    chip->setAlignment(Qt::AlignCenter);
    chip->setMinimumHeight(42);
    chip->setMinimumWidth(104);
    return chip;
  };

  playPauseChip_ = makeChip("Play", "aioYouTubePlayPauseChip");
  rewindChip_ = makeChip("-10 sec", "aioYouTubeRewindChip");
  forwardChip_ = makeChip("+10 sec", "aioYouTubeForwardChip");
  browseChip_ = makeChip("Browse Up Next", "aioYouTubeBrowseChip");
  backChip_ = makeChip("Back to Video", "aioYouTubeBackChip");

  transportLayout->addWidget(playPauseChip_);
  transportLayout->addWidget(rewindChip_);
  transportLayout->addWidget(forwardChip_);
  transportLayout->addStretch(1);
  transportLayout->addWidget(browseChip_);
  transportLayout->addWidget(backChip_);

  currentTimeLabel_ = new QLabel("0:00", this);
  currentTimeLabel_->setProperty("role", "ytSectionMeta");
  durationLabel_ = new QLabel("0:00", this);
  durationLabel_->setProperty("role", "ytSectionMeta");
  timelineSlider_ = new QSlider(Qt::Horizontal, this);
  timelineSlider_->setObjectName("aioYouTubeTimeline");
  timelineSlider_->setRange(0, 1000);
  timelineSlider_->setEnabled(false);

  auto *timelineCard = new QFrame(this);
  timelineCard->setObjectName("aioYouTubeTimelineCard");
  auto *timelineCardLayout = new QHBoxLayout(timelineCard);
  timelineCardLayout->setContentsMargins(18, 12, 18, 12);
  timelineCardLayout->setSpacing(14);
  timelineCardLayout->addWidget(currentTimeLabel_);
  timelineCardLayout->addWidget(timelineSlider_, 1);
  timelineCardLayout->addWidget(durationLabel_);

  recommendationsLabel_ = new QLabel("Recommended next", this);
  recommendationsLabel_->setProperty("role", "ytSectionTitle");

  recommendationsScroll_ = new QScrollArea(this);
  recommendationsScroll_->setWidgetResizable(true);
  recommendationsScroll_->setFrameShape(QFrame::NoFrame);
  recommendationsScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recommendationsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recommendationsScroll_->setFocusPolicy(Qt::NoFocus);
  recommendationsScroll_->setMinimumHeight(0);

  recommendationsOpacityEffect_ =
      new QGraphicsOpacityEffect(recommendationsScroll_);
  recommendationsOpacityEffect_->setOpacity(0.0);
  recommendationsScroll_->setGraphicsEffect(recommendationsOpacityEffect_);

  recommendationsHost_ = new QWidget(recommendationsScroll_);
  recommendationsLayout_ = new QHBoxLayout(recommendationsHost_);
  recommendationsLayout_->setContentsMargins(0, 0, 0, 0);
  recommendationsLayout_->setSpacing(16);
  recommendationsScroll_->setWidget(recommendationsHost_);

  overlayLayout->addWidget(titleLabel_);
  overlayLayout->addWidget(hintLabel_);
  overlayLayout->addWidget(transportBar_);
  overlayLayout->addWidget(timelineCard);
  overlayLayout->addWidget(recommendationsLabel_);
  overlayLayout->addWidget(recommendationsScroll_);

  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &url) { refreshRecommendationThumbnail(url); });

  refreshTransportChips();
}

void YouTubePlayerOverlay::setPlayerTitle(const QString &title) {
  titleLabel_->setText(title);
}

void YouTubePlayerOverlay::setPlaybackState(bool playing, bool ready) {
  playbackPlaying_ = playing;
  playbackReady_ = ready;
  refreshTransportChips();
}

void YouTubePlayerOverlay::setTitleVisible(bool visible) {
  titleLabel_->setVisible(visible);
}

void YouTubePlayerOverlay::setHintText(const QString &hint) {
  hintLabel_->setText(hint);
}

void YouTubePlayerOverlay::setHintVisible(bool visible) {
  hintLabel_->setVisible(visible);
}

void YouTubePlayerOverlay::syncTimeline(int currentSeconds,
                                        int durationSeconds) {
  const int boundedDuration = std::max(durationSeconds, 0);
  timelineSlider_->setEnabled(boundedDuration > 0);
  timelineSlider_->setRange(0, std::max(boundedDuration, 1));
  timelineSlider_->setValue(
      std::clamp(currentSeconds, 0, std::max(boundedDuration, 1)));
  currentTimeLabel_->setText(formatTime(std::max(currentSeconds, 0)));
  durationLabel_->setText(formatTime(std::max(boundedDuration, 0)));
}

void YouTubePlayerOverlay::setTimelineSelected(bool selected) {
  timelineSelected_ = selected;
  timelineSlider_->setProperty("selected", selected);
  timelineSlider_->style()->unpolish(timelineSlider_);
  timelineSlider_->style()->polish(timelineSlider_);
  timelineSlider_->update();
  refreshTransportChips();
}

void YouTubePlayerOverlay::setRecommendationsExpanded(bool expanded) {
  recommendationsExpanded_ = expanded;
  setProperty("recommendations_expanded", expanded);
  style()->unpolish(this);
  style()->polish(this);
  update();
  refreshTransportChips();
}

void YouTubePlayerOverlay::setRecommendedVideos(
    const std::vector<AIO::Streaming::VideoContent> &videos) {
  recommendedVideos_ = videos;
  selectedRecommendationIndex_ =
      std::clamp(selectedRecommendationIndex_, 0,
                 std::max(0, static_cast<int>(recommendedVideos_.size()) - 1));
  rebuildRecommendations();
}

void YouTubePlayerOverlay::setSelectedRecommendationIndex(int index,
                                                          bool focused) {
  selectedRecommendationIndex_ = index;
  recommendationsFocused_ = focused;
  refreshTransportChips();

  for (int i = 0; i < static_cast<int>(recommendationTiles_.size()); ++i) {
    auto *tile = recommendationTiles_[i];
    if (!tile) {
      continue;
    }
    tile->setProperty("selected", recommendationsFocused_ &&
                                      i == selectedRecommendationIndex_);
    tile->style()->unpolish(tile);
    tile->style()->polish(tile);
    tile->update();
  }
}

void YouTubePlayerOverlay::setRecommendationsVisible(bool showLabel,
                                                     bool showRail) {
  recommendationsLabel_->setVisible(showLabel);
  recommendationsScroll_->setMinimumHeight(
      showRail ? (recommendationsExpanded_ ? 292 : 184) : 0);
  if (showRail == recommendationsRailVisible_) {
    recommendationsScroll_->setVisible(showRail);
    if (recommendationsOpacityEffect_) {
      recommendationsOpacityEffect_->setOpacity(showRail ? 1.0 : 0.0);
    }
    return;
  }

  recommendationsRailVisible_ = showRail;
  recommendationsScroll_->setVisible(true);

  if (!recommendationsOpacityEffect_) {
    recommendationsScroll_->setVisible(showRail);
    return;
  }

  auto *anim = new QPropertyAnimation(recommendationsOpacityEffect_, "opacity",
                                      recommendationsScroll_);
  anim->setDuration(showRail ? 180 : 140);
  anim->setStartValue(recommendationsOpacityEffect_->opacity());
  anim->setEndValue(showRail ? 1.0 : 0.0);
  anim->setEasingCurve(showRail ? QEasingCurve::OutCubic
                                : QEasingCurve::InCubic);
  connect(anim, &QPropertyAnimation::finished, this,
          [this, showRail]() { recommendationsScroll_->setVisible(showRail); });
  anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void YouTubePlayerOverlay::ensureRecommendationVisible(int index) {
  if (index < 0 || index >= static_cast<int>(recommendationTiles_.size())) {
    return;
  }
  recommendationsScroll_->ensureWidgetVisible(recommendationTiles_[index], 24,
                                              24);
}

bool YouTubePlayerOverlay::hasRecommendations() const {
  return !recommendedVideos_.empty();
}

void YouTubePlayerOverlay::refreshTransportChips() {
  if (!playPauseChip_ || !rewindChip_ || !forwardChip_ || !browseChip_ ||
      !backChip_) {
    return;
  }

  playPauseChip_->setText(playbackPlaying_ ? QStringLiteral("Pause")
                                           : QStringLiteral("Play"));
  playPauseChip_->setProperty("active", timelineSelected_);
  playPauseChip_->setProperty("emphasis", playbackReady_);

  rewindChip_->setProperty("active", timelineSelected_);
  forwardChip_->setProperty("active", timelineSelected_);

  browseChip_->setText(recommendationsFocused_
                           ? QStringLiteral("Up Next Active")
                           : QStringLiteral("Browse Up Next"));
  browseChip_->setProperty("active",
                           recommendationsFocused_ || recommendationsExpanded_);
  browseChip_->setProperty("emphasis", !recommendedVideos_.empty());

  backChip_->setProperty("active",
                         !recommendationsFocused_ && !timelineSelected_);
  backChip_->setProperty("emphasis", true);

  const std::array<QLabel *, 5> chips = {playPauseChip_, rewindChip_,
                                         forwardChip_, browseChip_, backChip_};
  for (auto *chip : chips) {
    chip->style()->unpolish(chip);
    chip->style()->polish(chip);
    chip->update();
  }
}

void YouTubePlayerOverlay::refreshRecommendationThumbnail(const QString &url) {
  if (!recommendationsHost_ || url.trimmed().isEmpty()) {
    return;
  }

  const auto thumbs = recommendationsHost_->findChildren<QLabel *>(
      QStringLiteral("thumb"), Qt::FindChildrenRecursively);
  for (auto *thumb : thumbs) {
    if (!thumb || thumb->property(kThumbnailUrlProperty).toString() != url) {
      continue;
    }

    QPixmap pixmap;
    if (ThumbnailCache::instance().tryGet(url, &pixmap)) {
      if (auto *fillThumb = dynamic_cast<ThumbnailFillLabel *>(thumb)) {
        fillThumb->setSourcePixmap(pixmap);
      }
    }
  }
}

void YouTubePlayerOverlay::rebuildRecommendations() {
  while (QLayoutItem *item = recommendationsLayout_->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  recommendationTiles_.clear();

  if (recommendedVideos_.empty()) {
    auto *placeholder = new QLabel("Recommended videos will appear here.",
                                   recommendationsHost_);
    placeholder->setProperty("role", "ytSectionMeta");
    recommendationsLayout_->addWidget(placeholder);
    recommendationsLayout_->addStretch();
    return;
  }

  for (int i = 0; i < static_cast<int>(recommendedVideos_.size()); ++i) {
    auto *tile = new QFrame(recommendationsHost_);
    tile->setObjectName("aioYouTubeRecommendationTile");
    tile->setProperty("selected", recommendationsFocused_ &&
                                      i == selectedRecommendationIndex_);
    tile->setFixedSize(336, 236);

    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *thumbFrame = new QFrame(tile);
    thumbFrame->setObjectName("aioYouTubeRecommendationThumbFrame");

    auto *thumbFrameLayout = new QGridLayout(thumbFrame);
    thumbFrameLayout->setContentsMargins(0, 0, 0, 0);
    thumbFrameLayout->setSpacing(0);

    auto *thumb = new ThumbnailFillLabel(thumbFrame);
    thumb->setObjectName("thumb");
    thumb->setProperty("role", "thumb");
    thumb->setFixedSize(308, 172);
    thumb->setText("Loading");

    const QString categoryText =
        QString::fromStdString(recommendedVideos_[i].category).trimmed();
    const QString durationText =
        formatDurationLabel(recommendedVideos_[i].durationSeconds);

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

    thumbFrameLayout->addWidget(thumb, 0, 0);
    thumbFrameLayout->addWidget(thumbOverlay, 0, 0,
                                Qt::AlignLeft | Qt::AlignBottom);

    auto *title =
        new QLabel(QString::fromStdString(recommendedVideos_[i].title), tile);
    title->setProperty("role", "tileTitle");
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    title->setWordWrap(true);
    title->setMinimumHeight(48);
    title->setMaximumHeight(48);

    const QFontMetrics titleMetrics(title->font());
    const QString elidedTitle = titleMetrics.elidedText(
        title->text().simplified(), Qt::ElideRight, 420);
    title->setText(elidedTitle);

    auto *meta = new QLabel(compactSummary(recommendedVideos_[i]), tile);
    meta->setProperty("role", "tileMeta");
    meta->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    meta->setWordWrap(true);
    meta->setMaximumHeight(36);

    const QFontMetrics metaMetrics(meta->font());
    meta->setText(
        metaMetrics.elidedText(meta->text().simplified(), Qt::ElideRight, 380));

    layout->addWidget(thumbFrame);
    layout->addWidget(title);
    layout->addWidget(meta);
    layout->addStretch();

    const QString thumbUrl =
        QString::fromStdString(recommendedVideos_[i].thumbnailUrl);
    thumb->setProperty(kThumbnailUrlProperty, thumbUrl);
    if (!thumbUrl.isEmpty()) {
      QPixmap pixmap;
      if (ThumbnailCache::instance().tryGet(thumbUrl, &pixmap)) {
        thumb->setSourcePixmap(pixmap);
      } else {
        ThumbnailCache::instance().request(thumbUrl);
      }
    }

    recommendationsLayout_->addWidget(tile);
    recommendationTiles_.push_back(tile);
  }

  recommendationsLayout_->addStretch();
  setSelectedRecommendationIndex(selectedRecommendationIndex_,
                                 recommendationsFocused_);
}

} // namespace GUI
} // namespace AIO
