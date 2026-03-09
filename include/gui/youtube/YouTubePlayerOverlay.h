#pragma once

#include <QFrame>

#include <vector>

#include "streaming/StreamingService.h"

class QLabel;
class QScrollArea;
class QSlider;
class QHBoxLayout;

namespace AIO {
namespace GUI {

class YouTubePlayerOverlay final : public QFrame {
  Q_OBJECT

public:
  explicit YouTubePlayerOverlay(QWidget *parent = nullptr);

  void setPlayerTitle(const QString &title);
  void setHintText(const QString &hint);
  void syncTimeline(int currentSeconds, int durationSeconds);
  void setTimelineSelected(bool selected);
  void setRecommendedVideos(
      const std::vector<AIO::Streaming::VideoContent> &videos);
  void setSelectedRecommendationIndex(int index, bool focused);
  void setRecommendationsVisible(bool showLabel, bool showRail);
  void ensureRecommendationVisible(int index);
  bool hasRecommendations() const;

private:
  void rebuildRecommendations();

  QLabel *titleLabel_ = nullptr;
  QLabel *hintLabel_ = nullptr;
  QLabel *recommendationsLabel_ = nullptr;
  QLabel *currentTimeLabel_ = nullptr;
  QLabel *durationLabel_ = nullptr;
  QSlider *timelineSlider_ = nullptr;
  QScrollArea *recommendationsScroll_ = nullptr;
  QWidget *recommendationsHost_ = nullptr;
  QHBoxLayout *recommendationsLayout_ = nullptr;

  std::vector<AIO::Streaming::VideoContent> recommendedVideos_;
  std::vector<QFrame *> recommendationTiles_;
  int selectedRecommendationIndex_ = 0;
  bool recommendationsFocused_ = false;
};

} // namespace GUI
} // namespace AIO
