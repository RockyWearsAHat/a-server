#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "streaming/StreamingService.h"

class QFrame;
class QHBoxLayout;
class QToolButton;
class QLabel;
class QAudioOutput;
class QMediaPlayer;
class QVideoWidget;
class QVBoxLayout;
class QWidget;
class YouTubePlayerOverlay;

namespace AIO::Streaming {
class YouTubeService;
}

namespace AIO {
namespace GUI {

class YouTubePlayerPage final : public QWidget {
  Q_OBJECT

public:
  explicit YouTubePlayerPage(QWidget *parent = nullptr);

  void playVideoUrl(const QString &url);

signals:
  void homeRequested();
  void backRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  enum class FocusZone {
    Web,
    Timeline,
    Recommendations,
  };

  void setTopBarText(const QString &text);
  void updateStatusText(const QString &text);
  QString normalizeVideoUrl(const QString &url) const;
  QString extractVideoId(const QString &url) const;
  void setupOverlayUi();
  void requestRelatedVideos(const QString &videoId);
  void rebuildRecommendations();
  void updateRecommendationFocus();
  void activateRecommendation();
  void setFocusZone(FocusZone zone);
  void updateOverlayHints();
  void updatePlaybackChrome();
  bool handleKeyPress(QKeyEvent *event);
  void seekRelativeSeconds(int deltaSeconds);
  void togglePlayback();
  QString formatTime(int totalSeconds) const;
  void syncTimelineFromPlayback(int currentSeconds, int durationSeconds);
  void updatePlaybackSurfaceGeometry();
  void reloadCurrentVideo();

  QWidget *videoStage_ = nullptr;
  QFrame *surfaceFrame_ = nullptr;
  QVideoWidget *videoWidget_ = nullptr;
  QMediaPlayer *mediaPlayer_ = nullptr;
  QAudioOutput *audioOutput_ = nullptr;
  QWidget *chromeOverlay_ = nullptr;
  QWidget *topBar_ = nullptr;
  QLabel *titleLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  AIO::GUI::YouTubePlayerOverlay *overlayPanel_ = nullptr;
  QToolButton *backButton_ = nullptr;
  QToolButton *homeButton_ = nullptr;
  QToolButton *reloadButton_ = nullptr;

  std::vector<AIO::Streaming::VideoContent> recommendedVideos_;
  FocusZone focusZone_ = FocusZone::Web;
  int selectedRecommendationIndex_ = 0;
  int currentDurationSeconds_ = 0;
  int lastPersistedProgressSeconds_ = -1;
  QString currentVideoId_;
  QString currentPlaybackUrl_;
  bool playerReady_ = false;
  bool loadFailed_ = false;
  uint64_t playbackRequestSerial_ = 0;
  uint64_t relatedRequestSerial_ = 0;
  AIO::Streaming::YouTubeService *youTube_ = nullptr;

  // Menu input is routed centrally by MainWindow via synthesized key events.
};

} // namespace GUI
} // namespace AIO
