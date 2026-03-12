#pragma once

#include <QFrame>

#include <vector>

#include "streaming/StreamingService.h"

class QLabel;
class QScrollArea;
class QSlider;
class QHBoxLayout;
class QGraphicsOpacityEffect;

namespace AIO {
namespace GUI {

class YouTubePlayerOverlay final : public QFrame {
  Q_OBJECT

public:
  explicit YouTubePlayerOverlay(QWidget *parent = nullptr);

  void setPlayerTitle(const QString &title);
  void setPlaybackState(bool playing, bool ready);
  void setTitleVisible(bool visible);
  void setHintText(const QString &hint);
  void setHintVisible(bool visible);
  void syncTimeline(int currentSeconds, int durationSeconds);
  void setTimelineSelected(bool selected);
  void setRecommendationsExpanded(bool expanded);
  void
  setRecommendedVideos(const std::vector<AIO::Streaming::VideoContent> &videos);
  void setSelectedRecommendationIndex(int index, bool focused);
  void setRecommendationsVisible(bool showLabel, bool showRail);
  void ensureRecommendationVisible(int index);
  bool hasRecommendations() const;

private:
  void rebuildRecommendations();
  void refreshRecommendationThumbnail(const QString &url);
  void refreshTransportChips();

  QLabel *titleLabel_ = nullptr;
  QLabel *hintLabel_ = nullptr;
  QFrame *transportBar_ = nullptr;
  QLabel *playPauseChip_ = nullptr;
  QLabel *rewindChip_ = nullptr;
  QLabel *forwardChip_ = nullptr;
  QLabel *browseChip_ = nullptr;
  QLabel *backChip_ = nullptr;
  QLabel *recommendationsLabel_ = nullptr;
  QLabel *currentTimeLabel_ = nullptr;
  QLabel *durationLabel_ = nullptr;
  QSlider *timelineSlider_ = nullptr;
  QScrollArea *recommendationsScroll_ = nullptr;
  QWidget *recommendationsHost_ = nullptr;
  QHBoxLayout *recommendationsLayout_ = nullptr;
  QGraphicsOpacityEffect *recommendationsOpacityEffect_ = nullptr;

  std::vector<AIO::Streaming::VideoContent> recommendedVideos_;
  std::vector<QFrame *> recommendationTiles_;
  int selectedRecommendationIndex_ = 0;
  bool playbackReady_ = false;
  bool playbackPlaying_ = false;
  bool timelineSelected_ = false;
  bool recommendationsFocused_ = false;
  bool recommendationsExpanded_ = false;
  bool recommendationsRailVisible_ = false;
};

} // namespace GUI
} // namespace AIO
