#include "gui/youtube/YouTubePlayerOverlay.h"

#include "gui/ThumbnailCache.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

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

} // namespace

namespace AIO {
namespace GUI {

YouTubePlayerOverlay::YouTubePlayerOverlay(QWidget *parent) : QFrame(parent) {
  setObjectName("aioYouTubePlayerOverlay");

  auto *overlayLayout = new QVBoxLayout(this);
  overlayLayout->setContentsMargins(28, 18, 28, 22);
  overlayLayout->setSpacing(12);

  titleLabel_ = new QLabel("Now playing", this);
  titleLabel_->setProperty("role", "ytSectionTitle");

  hintLabel_ = new QLabel(this);
  hintLabel_->setProperty("role", "ytSectionMeta");
  hintLabel_->setWordWrap(true);

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
  timelineCardLayout->setContentsMargins(12, 8, 12, 8);
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

  recommendationsHost_ = new QWidget(recommendationsScroll_);
  recommendationsLayout_ = new QHBoxLayout(recommendationsHost_);
  recommendationsLayout_->setContentsMargins(0, 0, 0, 0);
  recommendationsLayout_->setSpacing(14);
  recommendationsScroll_->setWidget(recommendationsHost_);

  overlayLayout->addWidget(titleLabel_);
  overlayLayout->addWidget(hintLabel_);
  overlayLayout->addWidget(timelineCard);
  overlayLayout->addWidget(recommendationsLabel_);
  overlayLayout->addWidget(recommendationsScroll_);

  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &) { rebuildRecommendations(); });
}

void YouTubePlayerOverlay::setPlayerTitle(const QString &title) {
  titleLabel_->setText(title);
}

void YouTubePlayerOverlay::setHintText(const QString &hint) {
  hintLabel_->setText(hint);
}

void YouTubePlayerOverlay::syncTimeline(int currentSeconds, int durationSeconds) {
  const int boundedDuration = std::max(durationSeconds, 0);
  timelineSlider_->setEnabled(boundedDuration > 0);
  timelineSlider_->setRange(0, std::max(boundedDuration, 1));
  timelineSlider_->setValue(
      std::clamp(currentSeconds, 0, std::max(boundedDuration, 1)));
  currentTimeLabel_->setText(formatTime(std::max(currentSeconds, 0)));
  durationLabel_->setText(formatTime(std::max(boundedDuration, 0)));
}

void YouTubePlayerOverlay::setTimelineSelected(bool selected) {
  timelineSlider_->setProperty("selected", selected);
  timelineSlider_->style()->unpolish(timelineSlider_);
  timelineSlider_->style()->polish(timelineSlider_);
  timelineSlider_->update();
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

  for (int i = 0; i < static_cast<int>(recommendationTiles_.size()); ++i) {
    auto *tile = recommendationTiles_[i];
    if (!tile) {
      continue;
    }
    tile->setProperty("selected",
                      recommendationsFocused_ && i == selectedRecommendationIndex_);
    tile->style()->unpolish(tile);
    tile->style()->polish(tile);
    tile->update();
  }
}

void YouTubePlayerOverlay::setRecommendationsVisible(bool showLabel,
                                                     bool showRail) {
  recommendationsLabel_->setVisible(showLabel);
  recommendationsScroll_->setVisible(showRail);
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

void YouTubePlayerOverlay::rebuildRecommendations() {
  while (QLayoutItem *item = recommendationsLayout_->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  recommendationTiles_.clear();

  if (recommendedVideos_.empty()) {
    auto *placeholder =
        new QLabel("Recommended videos will appear here.", recommendationsHost_);
    placeholder->setProperty("role", "ytSectionMeta");
    recommendationsLayout_->addWidget(placeholder);
    recommendationsLayout_->addStretch();
    return;
  }

  for (int i = 0; i < static_cast<int>(recommendedVideos_.size()); ++i) {
    auto *tile = new QFrame(recommendationsHost_);
    tile->setObjectName("aioYouTubeRecommendationTile");
    tile->setProperty("selected",
                      recommendationsFocused_ && i == selectedRecommendationIndex_);
    tile->setFixedSize(232, 144);

    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *thumb = new QLabel(tile);
    thumb->setObjectName("thumb");
    thumb->setProperty("role", "thumb");
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setFixedHeight(82);
    thumb->setText("Loading");

    auto *title =
        new QLabel(QString::fromStdString(recommendedVideos_[i].title), tile);
    title->setProperty("role", "tileTitle");
    title->setWordWrap(true);

    const QString categoryText =
        QString::fromStdString(recommendedVideos_[i].category).trimmed();
    const QString durationText =
        formatDurationLabel(recommendedVideos_[i].durationSeconds);

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

    const bool hasChips = !categoryText.isEmpty() || !durationText.isEmpty();

    layout->addWidget(thumb);
    layout->addWidget(title);
    if (hasChips) {
      layout->addWidget(chipRow);
    }
    layout->addStretch();

    const QString thumbUrl =
        QString::fromStdString(recommendedVideos_[i].thumbnailUrl);
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

    recommendationsLayout_->addWidget(tile);
    recommendationTiles_.push_back(tile);
  }

  recommendationsLayout_->addStretch();
  setSelectedRecommendationIndex(selectedRecommendationIndex_,
                                 recommendationsFocused_);
}

} // namespace GUI
} // namespace AIO
