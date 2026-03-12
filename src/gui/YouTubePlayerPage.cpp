#include "gui/YouTubePlayerPage.h"

#include "gui/youtube/YouTubePlayerOverlay.h"
#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QGraphicsBlurEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimediaWidgets/QVideoWidget>

#include <thread>

namespace AIO {
namespace GUI {

namespace {

QString extractYouTubeVideoId(const QString &url) {
  const QUrl parsed(url);
  if (!parsed.isValid()) {
    return QString();
  }

  const QString host = parsed.host();
  if (host.contains(QStringLiteral("youtu.be"))) {
    return parsed.path().mid(1);
  }
  if (host.contains(QStringLiteral("youtube.com"))) {
    if (parsed.path().startsWith(QStringLiteral("/embed/"))) {
      return parsed.path().section('/', 2, 2);
    }
    const QUrlQuery query(parsed);
    return query.queryItemValue(QStringLiteral("v"));
  }

  return QString();
}

void prefetchLikelyNextStreams(
    AIO::Streaming::YouTubeService *service,
    const std::vector<AIO::Streaming::VideoContent> &videos) {
  if (!service || videos.empty()) {
    return;
  }

  std::vector<std::string> ids;
  ids.reserve(2);
  for (const auto &video : videos) {
    std::string id = video.id;
    if (id.empty()) {
      id = extractYouTubeVideoId(QString::fromStdString(video.videoUrl))
               .toStdString();
    }
    if (id.empty()) {
      continue;
    }
    if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
      continue;
    }

    ids.push_back(id);
    if (ids.size() >= 2) {
      break;
    }
  }

  if (ids.empty()) {
    return;
  }

  std::thread([service, ids = std::move(ids)]() {
    for (const auto &id : ids) {
      service->resolvePlaybackStream(id);
    }
  }).detach();
}

} // namespace

YouTubePlayerPage::YouTubePlayerPage(QWidget *parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);

  auto service = AIO::Streaming::StreamingManager::getInstance().getService(
      AIO::Streaming::StreamingServiceType::YouTube);
  youTube_ = dynamic_cast<AIO::Streaming::YouTubeService *>(service.get());

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  videoStage_ = new QFrame(this);
  videoStage_->setObjectName("aioYouTubeVideoStage");
  auto *stageLayout = new QGridLayout(videoStage_);
  stageLayout->setContentsMargins(0, 0, 0, 0);
  stageLayout->setSpacing(0);

  surfaceFrame_ = new QFrame(videoStage_);
  surfaceFrame_->setObjectName("aioYouTubePlaybackSurface");
  surfaceFrame_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto *surfaceLayout = new QVBoxLayout(surfaceFrame_);
  surfaceLayout->setContentsMargins(0, 0, 0, 0);
  surfaceLayout->setSpacing(0);

  videoWidget_ = new QVideoWidget(surfaceFrame_);
  videoWidget_->setObjectName("aioYouTubePlaybackView");
  videoWidget_->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
  videoWidget_->setFocusPolicy(Qt::StrongFocus);
  videoWidget_->installEventFilter(this);
  surfaceLayout->addWidget(videoWidget_);

  audioOutput_ = new QAudioOutput(this);
  audioOutput_->setVolume(1.0f);

  mediaPlayer_ = new QMediaPlayer(this);
  mediaPlayer_->setAudioOutput(audioOutput_);
  mediaPlayer_->setVideoOutput(videoWidget_);
  stageLayout->addWidget(surfaceFrame_, 0, 0, Qt::AlignCenter);

  chromeOverlay_ = new QWidget(videoStage_);
  chromeOverlay_->setObjectName("aioYouTubeChromeOverlay");
  auto *chromeLayout = new QVBoxLayout(chromeOverlay_);
  chromeLayout->setContentsMargins(22, 18, 22, 18);
  chromeLayout->setSpacing(10);

  centerStageCard_ = new QFrame(videoStage_);
  centerStageCard_->setObjectName("aioYouTubeCenterStageCard");
  centerStageCard_->setSizePolicy(QSizePolicy::MinimumExpanding,
                                  QSizePolicy::Minimum);
  centerStageCard_->setMaximumWidth(640);

  auto *centerCardLayout = new QVBoxLayout(centerStageCard_);
  centerCardLayout->setContentsMargins(28, 24, 28, 24);
  centerCardLayout->setSpacing(10);

  centerStageEyebrowLabel_ = new QLabel("YOUTUBE", centerStageCard_);
  centerStageEyebrowLabel_->setObjectName("aioYouTubeCenterStageEyebrow");

  centerStageTitleLabel_ = new QLabel("Preparing playback", centerStageCard_);
  centerStageTitleLabel_->setObjectName("aioYouTubeCenterStageTitle");
  centerStageTitleLabel_->setWordWrap(true);

  centerStageBodyLabel_ =
      new QLabel("Resolving the best stream for this video.", centerStageCard_);
  centerStageBodyLabel_->setObjectName("aioYouTubeCenterStageBody");
  centerStageBodyLabel_->setWordWrap(true);

  centerStageActionLabel_ = new QLabel(
      "Down opens transport controls. Esc goes back.", centerStageCard_);
  centerStageActionLabel_->setObjectName("aioYouTubeCenterStageAction");
  centerStageActionLabel_->setWordWrap(true);

  centerCardLayout->addWidget(centerStageEyebrowLabel_);
  centerCardLayout->addWidget(centerStageTitleLabel_);
  centerCardLayout->addWidget(centerStageBodyLabel_);
  centerCardLayout->addWidget(centerStageActionLabel_);

  topBar_ = new QWidget(chromeOverlay_);
  topBar_->setObjectName("aioTopBar");
  topBar_->setProperty("player_chrome", true);

  auto *barLayout = new QHBoxLayout(topBar_);
  barLayout->setContentsMargins(14, 10, 14, 10);
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

  reloadButton_ = new QToolButton(topBar_);
  reloadButton_->setText("Reload");
  reloadButton_->setAutoRaise(true);
  reloadButton_->setFocusPolicy(Qt::NoFocus);
  reloadButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("YouTube", topBar_);
  titleLabel_->setProperty("role", "ytPlayerHeaderTitle");

  stateChipLabel_ = new QLabel("Loading", topBar_);
  stateChipLabel_->setObjectName("aioYouTubePlayerStateChip");
  stateChipLabel_->setAlignment(Qt::AlignCenter);

  focusChipLabel_ = new QLabel("Video", topBar_);
  focusChipLabel_->setObjectName("aioYouTubePlayerFocusChip");
  focusChipLabel_->setAlignment(Qt::AlignCenter);

  barLayout->addWidget(backButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addWidget(reloadButton_);
  barLayout->addSpacing(10);
  barLayout->addWidget(titleLabel_, 1);
  barLayout->addWidget(stateChipLabel_);
  barLayout->addWidget(focusChipLabel_);

  statusLabel_ = new QLabel(chromeOverlay_);
  statusLabel_->setObjectName("aioYouTubePlayerStatus");
  statusLabel_->setProperty("role", "ytSectionMeta");
  statusLabel_->setContentsMargins(0, 0, 0, 0);

  setupOverlayUi();
  if (overlayPanel_) {
    overlayPanel_->setTitleVisible(true);
    overlayPanel_->setProperty("player_dock", false);
    overlayPanel_->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::MinimumExpanding);
  }

  chromeLayout->addWidget(topBar_);
  chromeLayout->addWidget(statusLabel_, 0, Qt::AlignLeft);
  chromeLayout->addStretch(1);
  chromeLayout->addWidget(overlayPanel_);
  stageLayout->addWidget(chromeOverlay_, 0, 0);
  stageLayout->addWidget(centerStageCard_, 0, 0, Qt::AlignCenter);

  root->addWidget(videoStage_, 1);
  updatePlaybackSurfaceGeometry();

  connect(backButton_, &QToolButton::clicked, this,
          [this]() { emit backRequested(); });
  connect(homeButton_, &QToolButton::clicked, this,
          [this]() { emit homeRequested(); });
  connect(reloadButton_, &QToolButton::clicked, this,
          [this]() { reloadCurrentVideo(); });

  connect(mediaPlayer_, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
            switch (status) {
            case QMediaPlayer::LoadingMedia:
            case QMediaPlayer::BufferingMedia:
            case QMediaPlayer::BufferedMedia:
              if (!playerReady_ && !loadFailed_) {
                updateStatusText(QStringLiteral("Preparing playback..."));
              }
              break;
            case QMediaPlayer::InvalidMedia:
              playerReady_ = false;
              loadFailed_ = true;
              updateStatusText(
                  QStringLiteral("Video stream could not be loaded"));
              updatePlaybackChrome();
              break;
            case QMediaPlayer::EndOfMedia:
              updateStatusText(QStringLiteral("Playback finished"));
              updatePlaybackChrome();
              break;
            default:
              break;
            }
          });
  connect(mediaPlayer_, &QMediaPlayer::playbackStateChanged, this,
          [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::PlayingState && playerReady_ &&
                !loadFailed_) {
              updateStatusText(QStringLiteral("Playback ready"));
            }
            updatePlaybackChrome();
          });
  connect(mediaPlayer_, &QMediaPlayer::positionChanged, this,
          [this](qint64 position) {
            const int currentSeconds =
                static_cast<int>(std::max<qint64>(0, position / 1000));
            const int durationSeconds = static_cast<int>(
                std::max<qint64>(0, mediaPlayer_->duration() / 1000));
            syncTimelineFromPlayback(currentSeconds, durationSeconds);
            if (youTube_ && !currentVideoId_.isEmpty() && currentSeconds > 0 &&
                (lastPersistedProgressSeconds_ < 0 ||
                 currentSeconds >= lastPersistedProgressSeconds_ + 15)) {
              youTube_->updateWatchProgress(currentVideoId_.toStdString(),
                                            currentSeconds);
              lastPersistedProgressSeconds_ = currentSeconds;
            }
          });
  connect(mediaPlayer_, &QMediaPlayer::durationChanged, this,
          [this](qint64 duration) {
            syncTimelineFromPlayback(
                static_cast<int>(
                    std::max<qint64>(0, mediaPlayer_->position() / 1000)),
                static_cast<int>(std::max<qint64>(0, duration / 1000)));
          });
  connect(mediaPlayer_, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &errorString) {
            playerReady_ = false;
            loadFailed_ = true;
            updateStatusText(errorString.trimmed().isEmpty()
                                 ? QStringLiteral("Video playback failed")
                                 : errorString.trimmed());
            updatePlaybackChrome();
          });
  connect(videoWidget_, &QVideoWidget::fullScreenChanged, this,
          [this](bool fullScreen) {
            if (fullScreen) {
              topBar_->hide();
              statusLabel_->hide();
              if (overlayPanel_) {
                overlayPanel_->hide();
              }
            } else {
              topBar_->show();
              statusLabel_->show();
              if (overlayPanel_) {
                overlayPanel_->show();
              }
            }
          });

  connect(mediaPlayer_, &QMediaPlayer::sourceChanged, this, [this]() {
    playerReady_ = false;
    loadFailed_ = false;
    updateStatusText("Loading video...");
    updatePlaybackChrome();
  });

  updateStatusText("Ready");
  updateOverlayHints();
  updatePlaybackChrome();
  updateCenterStageCard();
}

void YouTubePlayerPage::setupOverlayUi() {
  overlayPanel_ =
      new YouTubePlayerOverlay(chromeOverlay_ ? chromeOverlay_ : this);
}

void YouTubePlayerPage::setTopBarText(const QString &text) {
  titleLabel_->setText(text);
}

void YouTubePlayerPage::updateStatusText(const QString &text) {
  statusLabel_->setText(text);
  updateCenterStageCard();
}

QString YouTubePlayerPage::normalizeVideoUrl(const QString &url) const {
  const QString videoId = extractVideoId(url);
  if (videoId.isEmpty()) {
    return url;
  }

  QUrl watch(QStringLiteral("https://www.youtube.com/watch"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("autoplay"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("v"), videoId);
  query.addQueryItem(QStringLiteral("app"), QStringLiteral("desktop"));
  query.addQueryItem(QStringLiteral("persist_app"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("has_verified"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("bpctr"), QStringLiteral("9999999999"));
  query.addQueryItem(QStringLiteral("noapp"), QStringLiteral("1"));
  watch.setQuery(query);
  return watch.toString();
}

QString YouTubePlayerPage::extractVideoId(const QString &url) const {
  const QUrl parsed(url);
  if (!parsed.isValid()) {
    return QString();
  }

  const QString host = parsed.host();
  if (host.contains(QStringLiteral("youtu.be"))) {
    return parsed.path().mid(1);
  }
  if (host.contains(QStringLiteral("youtube.com"))) {
    if (parsed.path().startsWith(QStringLiteral("/embed/"))) {
      return parsed.path().section('/', 2, 2);
    }
    const QUrlQuery query(parsed);
    return query.queryItemValue(QStringLiteral("v"));
  }
  return QString();
}

void YouTubePlayerPage::playVideoUrl(const QString &url) {
  setTopBarText("YouTube");
  updateStatusText("Opening video...");
  currentPlaybackUrl_ = url;
  currentVideoId_ = extractVideoId(url);
  lastPersistedProgressSeconds_ = -1;
  playerReady_ = false;
  loadFailed_ = false;
  syncTimelineFromPlayback(0, 0);
  requestRelatedVideos(currentVideoId_);
  updateCenterStageCard();
  if (mediaPlayer_) {
    mediaPlayer_->stop();
    mediaPlayer_->setSource(QUrl());
  }

  if (currentVideoId_.isEmpty()) {
    if (mediaPlayer_) {
      mediaPlayer_->setSource(QUrl(url));
      mediaPlayer_->play();
    }
  } else {
    updateStatusText("Resolving stream...");
    const uint64_t requestId = ++playbackRequestSerial_;
    const QString videoId = currentVideoId_;
    QPointer<YouTubePlayerPage> guard(this);
    auto *service = youTube_;
    std::thread([guard, requestId, service, videoId]() {
      const auto stream =
          service ? service->resolvePlaybackStream(videoId.toStdString())
                  : std::nullopt;
      if (!guard) {
        return;
      }
      QMetaObject::invokeMethod(
          guard.data(),
          [guard, requestId, videoId, stream]() {
            if (!guard || requestId != guard->playbackRequestSerial_ ||
                videoId != guard->currentVideoId_) {
              return;
            }
            if (!stream.has_value()) {
              guard->loadFailed_ = true;
              guard->playerReady_ = false;
              guard->updateStatusText(
                  QStringLiteral("Could not resolve a local playback stream"));
              guard->updatePlaybackChrome();
              guard->updateCenterStageCard();
              return;
            }

            const QString title =
                QString::fromStdString(stream->title).trimmed();
            guard->setTopBarText(title.isEmpty() ? QStringLiteral("YouTube")
                                                 : title);
            if (guard->overlayPanel_) {
              guard->overlayPanel_->setPlayerTitle(
                  title.isEmpty() ? QStringLiteral("Now playing") : title);
            }
            guard->currentPlaybackUrl_ =
                QString::fromStdString(stream->webpageUrl);
            guard->updateStatusText(QStringLiteral("Preparing playback..."));
            if (guard->mediaPlayer_) {
              guard->mediaPlayer_->setSource(
                  QUrl(QString::fromStdString(stream->streamUrl)));
              guard->mediaPlayer_->play();
            }
            guard->updateCenterStageCard();
          },
          Qt::QueuedConnection);
    }).detach();
  }
  if (videoWidget_) {
    videoWidget_->setFocus();
  }
}

void YouTubePlayerPage::requestRelatedVideos(const QString &videoId) {
  recommendedVideos_.clear();
  selectedRecommendationIndex_ = 0;
  rebuildRecommendations();

  if (!youTube_ || videoId.isEmpty()) {
    updateOverlayHints();
    updateCenterStageCard();
    return;
  }

  const uint64_t requestId = ++relatedRequestSerial_;
  QPointer<YouTubePlayerPage> guard(this);
  auto *service = youTube_;
  const std::string id = videoId.toStdString();
  std::thread([guard, requestId, service, id]() {
    const auto videos = service ? service->getRelatedVideos(id, 18)
                                : std::vector<AIO::Streaming::VideoContent>{};
    if (!guard) {
      return;
    }
    QMetaObject::invokeMethod(
        guard.data(),
        [guard, requestId, videos]() mutable {
          if (!guard || requestId != guard->relatedRequestSerial_) {
            return;
          }
          guard->recommendedVideos_ = videos;
          prefetchLikelyNextStreams(guard->youTube_, guard->recommendedVideos_);
          guard->selectedRecommendationIndex_ = 0;
          guard->rebuildRecommendations();
          guard->updateOverlayHints();
        },
        Qt::QueuedConnection);
  }).detach();
}

void YouTubePlayerPage::rebuildRecommendations() {
  if (!overlayPanel_) {
    return;
  }
  overlayPanel_->setRecommendedVideos(recommendedVideos_);
  updateRecommendationFocus();
}

void YouTubePlayerPage::updateRecommendationFocus() {
  if (!overlayPanel_) {
    return;
  }
  overlayPanel_->setSelectedRecommendationIndex(
      selectedRecommendationIndex_, focusZone_ == FocusZone::Recommendations);
  if (focusZone_ == FocusZone::Recommendations) {
    overlayPanel_->ensureRecommendationVisible(selectedRecommendationIndex_);
  }
}

void YouTubePlayerPage::activateRecommendation() {
  if (selectedRecommendationIndex_ < 0 ||
      selectedRecommendationIndex_ >=
          static_cast<int>(recommendedVideos_.size())) {
    return;
  }
  playVideoUrl(QString::fromStdString(
      recommendedVideos_[selectedRecommendationIndex_].videoUrl));
}

void YouTubePlayerPage::setFocusZone(FocusZone zone) {
  focusZone_ = zone;
  if (focusZone_ == FocusZone::Web) {
    if (videoWidget_) {
      videoWidget_->setFocus(Qt::OtherFocusReason);
    }
  } else if (!hasFocus()) {
    setFocus(Qt::OtherFocusReason);
  }
  if (overlayPanel_) {
    overlayPanel_->setTimelineSelected(focusZone_ == FocusZone::Timeline);
  }
  updateRecommendationFocus();
  updateOverlayHints();
  updatePlaybackChrome();
}

void YouTubePlayerPage::updateOverlayHints() {
  QString hint;
  switch (focusZone_) {
  case FocusZone::Web:
    hint = QStringLiteral(
        "Press Enter to play or pause, Down to open transport, and Tab to "
        "cycle into Up Next. J, K, and L stay mapped to seek and toggle "
        "playback.");
    break;
  case FocusZone::Timeline:
    hint = QStringLiteral(
        "Transport is active: Left and right seek 10 seconds, Select "
        "toggles playback, and Down opens the full Up Next shelf.");
    break;
  case FocusZone::Recommendations:
    hint = QStringLiteral(
        "Up Next is active over the current video. Select switches videos, "
        "Up returns to transport, and U toggles between transport and Up "
        "Next.");
    break;
  }
  if (recommendedVideos_.empty() && focusZone_ != FocusZone::Web) {
    hint += QStringLiteral(" Related videos are still loading.");
  }
  if (overlayPanel_) {
    overlayPanel_->setHintText(hint);
  }
}

void YouTubePlayerPage::updatePlaybackChrome() {
  const bool playing = mediaPlayer_ && mediaPlayer_->playbackState() ==
                                           QMediaPlayer::PlayingState;
  const bool recommendationsFocused = focusZone_ == FocusZone::Recommendations;
  const bool timelineFocused = focusZone_ == FocusZone::Timeline;
  const bool compactChrome =
      playing && !loadFailed_ && focusZone_ == FocusZone::Web;
  const bool showTopChrome = !compactChrome || loadFailed_;
  const bool showOverlay = !compactChrome || loadFailed_ || timelineFocused ||
                           recommendationsFocused;
  const bool showStatus =
      loadFailed_ || !playerReady_ || !playing ||
      !statusLabel_->text().trimmed().isEmpty() && focusZone_ != FocusZone::Web;
  const bool hasRecommendations = !recommendedVideos_.empty();
  const bool showRecommendationsLabel =
      hasRecommendations &&
      (timelineFocused || recommendationsFocused || loadFailed_);
  const bool showRecommendationsRail =
      hasRecommendations &&
      (timelineFocused || recommendationsFocused || loadFailed_);

  topBar_->setVisible(showTopChrome);
  statusLabel_->setVisible(showStatus &&
                           !statusLabel_->text().trimmed().isEmpty());
  if (overlayPanel_) {
    overlayPanel_->setPlaybackState(playing, playerReady_ && !loadFailed_);
    overlayPanel_->setRecommendationsExpanded(recommendationsFocused ||
                                              loadFailed_);
    overlayPanel_->setVisible(showOverlay);
    overlayPanel_->setTitleVisible(true);
    overlayPanel_->setHintVisible(timelineFocused || recommendationsFocused ||
                                  loadFailed_ || !playerReady_);
    overlayPanel_->setRecommendationsVisible(showRecommendationsLabel,
                                             showRecommendationsRail);
  }

  const bool recommendationsActive =
      recommendationsFocused && hasRecommendations;
  applyVideoBlur(recommendationsActive);
  updateStateChips();
  refreshChromeStateProperties(recommendationsActive, compactChrome);
  updateCenterStageCard();
}

void YouTubePlayerPage::updateStateChips() {
  if (!stateChipLabel_ || !focusChipLabel_) {
    return;
  }

  const bool playing = mediaPlayer_ && mediaPlayer_->playbackState() ==
                                           QMediaPlayer::PlayingState;
  QString stateText;
  QString stateKind;
  if (loadFailed_) {
    stateText = QStringLiteral("Needs attention");
    stateKind = QStringLiteral("error");
  } else if (!playerReady_) {
    stateText = QStringLiteral("Loading");
    stateKind = QStringLiteral("loading");
  } else if (playing) {
    stateText = QStringLiteral("Playing");
    stateKind = QStringLiteral("playing");
  } else {
    stateText = QStringLiteral("Paused");
    stateKind = QStringLiteral("paused");
  }

  QString focusText;
  switch (focusZone_) {
  case FocusZone::Web:
    focusText = QStringLiteral("Video");
    break;
  case FocusZone::Timeline:
    focusText = QStringLiteral("Transport");
    break;
  case FocusZone::Recommendations:
    focusText = QStringLiteral("Up Next");
    break;
  }

  stateChipLabel_->setText(stateText);
  stateChipLabel_->setProperty("state", stateKind);
  stateChipLabel_->style()->unpolish(stateChipLabel_);
  stateChipLabel_->style()->polish(stateChipLabel_);
  stateChipLabel_->update();

  focusChipLabel_->setText(focusText);
  focusChipLabel_->setProperty("active", focusZone_ != FocusZone::Web ||
                                             !recommendedVideos_.empty());
  focusChipLabel_->style()->unpolish(focusChipLabel_);
  focusChipLabel_->style()->polish(focusChipLabel_);
  focusChipLabel_->update();
}

void YouTubePlayerPage::updateCenterStageCard() {
  if (!centerStageCard_ || !centerStageTitleLabel_ || !centerStageBodyLabel_ ||
      !centerStageActionLabel_) {
    return;
  }

  const bool playing = mediaPlayer_ && mediaPlayer_->playbackState() ==
                                           QMediaPlayer::PlayingState;
  const bool showCard = loadFailed_ || !playerReady_ || !playing;

  QString title = QStringLiteral("Preparing playback");
  QString body = statusLabel_ ? statusLabel_->text().trimmed() : QString();
  QString action =
      QStringLiteral("Down opens transport controls. Esc goes back to browse.");

  if (loadFailed_) {
    title = QStringLiteral("Playback needs attention");
    if (body.isEmpty()) {
      body = QStringLiteral("The current stream could not be started.");
    }
    action =
        QStringLiteral("Press Reload to try again, or browse Up Next below.");
  } else if (!playerReady_) {
    if (currentVideoId_.isEmpty()) {
      title = QStringLiteral("Opening video");
      if (body.isEmpty()) {
        body = QStringLiteral("Handing the video URL to the player.");
      }
    } else {
      title = QStringLiteral("Resolving stream");
      if (body.isEmpty()) {
        body = QStringLiteral(
            "Fetching a local playback URL and priming the player.");
      }
    }
    action = QStringLiteral(
        "Back, Home, Reload, and Up Next stay available while the player "
        "warms up.");
  } else if (!playing) {
    title = QStringLiteral("Ready to play");
    if (body.isEmpty()) {
      body = QStringLiteral("Use Select to start or resume playback.");
    }
    action = QStringLiteral(
        "Left and right seek 10 seconds once transport is selected, and U "
        "jumps into Up Next.");
  }

  centerStageTitleLabel_->setText(title);
  centerStageBodyLabel_->setText(body);
  centerStageActionLabel_->setText(action);
  centerStageCard_->setVisible(showCard);
  centerStageCard_->raise();
}

bool YouTubePlayerPage::handleKeyPress(QKeyEvent *event) {
  if (!event) {
    return false;
  }

  auto cycleFocusZone = [this](int direction) {
    if (direction > 0) {
      if (focusZone_ == FocusZone::Web) {
        setFocusZone(FocusZone::Timeline);
      } else if (focusZone_ == FocusZone::Timeline &&
                 !recommendedVideos_.empty()) {
        setFocusZone(FocusZone::Recommendations);
      } else {
        setFocusZone(FocusZone::Web);
      }
      return;
    }

    if (focusZone_ == FocusZone::Recommendations) {
      setFocusZone(FocusZone::Timeline);
    } else if (focusZone_ == FocusZone::Timeline) {
      setFocusZone(FocusZone::Web);
    } else if (!recommendedVideos_.empty()) {
      setFocusZone(FocusZone::Recommendations);
    }
  };

  if (event->key() == Qt::Key_Tab) {
    cycleFocusZone((event->modifiers() & Qt::ShiftModifier) ? -1 : 1);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_J) {
    seekRelativeSeconds(-10);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_K) {
    togglePlayback();
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_L &&
      !(event->modifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    seekRelativeSeconds(10);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_U && !recommendedVideos_.empty()) {
    setFocusZone(focusZone_ == FocusZone::Recommendations
                     ? FocusZone::Timeline
                     : FocusZone::Recommendations);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_H) {
    emit homeRequested();
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_R &&
      !(event->modifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
    reloadCurrentVideo();
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_Backspace && focusZone_ != FocusZone::Web) {
    setFocusZone(FocusZone::Web);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
    emit backRequested();
    event->accept();
    return true;
  }

  if (focusZone_ == FocusZone::Web &&
      (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
       event->key() == Qt::Key_Space)) {
    togglePlayback();
    event->accept();
    return true;
  }

  if (focusZone_ == FocusZone::Web &&
      (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up ||
       event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
    setFocusZone(FocusZone::Timeline);
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_Down) {
    if (focusZone_ == FocusZone::Web) {
      setFocusZone(FocusZone::Timeline);
      event->accept();
      return true;
    }
    if (focusZone_ == FocusZone::Timeline && !recommendedVideos_.empty()) {
      setFocusZone(FocusZone::Recommendations);
      event->accept();
      return true;
    }
  }

  if (event->key() == Qt::Key_Up) {
    if (focusZone_ == FocusZone::Recommendations) {
      setFocusZone(FocusZone::Timeline);
      event->accept();
      return true;
    }
    if (focusZone_ == FocusZone::Timeline) {
      setFocusZone(FocusZone::Web);
      event->accept();
      return true;
    }
  }

  if (focusZone_ == FocusZone::Timeline) {
    if (event->key() == Qt::Key_Left) {
      seekRelativeSeconds(-10);
      event->accept();
      return true;
    }
    if (event->key() == Qt::Key_Right) {
      seekRelativeSeconds(10);
      event->accept();
      return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
      togglePlayback();
      event->accept();
      return true;
    }
  }

  if (focusZone_ == FocusZone::Recommendations) {
    if (event->key() == Qt::Key_Left && selectedRecommendationIndex_ > 0) {
      selectedRecommendationIndex_--;
      updateRecommendationFocus();
      event->accept();
      return true;
    }
    if (event->key() == Qt::Key_Right &&
        selectedRecommendationIndex_ + 1 <
            static_cast<int>(recommendedVideos_.size())) {
      selectedRecommendationIndex_++;
      updateRecommendationFocus();
      event->accept();
      return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
      activateRecommendation();
      event->accept();
      return true;
    }
  }

  if (event->key() == Qt::Key_F5 ||
      ((event->modifiers() & Qt::ControlModifier) &&
       event->key() == Qt::Key_R)) {
    reloadCurrentVideo();
    event->accept();
    return true;
  }

  if (focusZone_ == FocusZone::Web && (event->modifiers() & Qt::AltModifier) &&
      event->key() == Qt::Key_Left) {
    emit backRequested();
    event->accept();
    return true;
  }

  return false;
}

bool YouTubePlayerPage::eventFilter(QObject *watched, QEvent *event) {
  if (watched == videoWidget_ && event) {
    if (event->type() == QEvent::KeyPress) {
      if (handleKeyPress(static_cast<QKeyEvent *>(event))) {
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void YouTubePlayerPage::seekRelativeSeconds(int deltaSeconds) {
  if (!mediaPlayer_) {
    return;
  }
  const qint64 duration = std::max<qint64>(0, mediaPlayer_->duration());
  const qint64 current = std::max<qint64>(0, mediaPlayer_->position());
  const qint64 next = std::clamp<qint64>(
      current + static_cast<qint64>(deltaSeconds) * 1000, 0, duration);
  mediaPlayer_->setPosition(next);
}

void YouTubePlayerPage::togglePlayback() {
  if (!mediaPlayer_) {
    return;
  }
  if (mediaPlayer_->playbackState() == QMediaPlayer::PlayingState) {
    mediaPlayer_->pause();
  } else {
    mediaPlayer_->play();
  }
}

void YouTubePlayerPage::reloadCurrentVideo() {
  if (currentPlaybackUrl_.isEmpty()) {
    return;
  }
  playVideoUrl(currentPlaybackUrl_);
}

QString YouTubePlayerPage::formatTime(int totalSeconds) const {
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

void YouTubePlayerPage::syncTimelineFromPlayback(int currentSeconds,
                                                 int durationSeconds) {
  currentDurationSeconds_ = std::max(durationSeconds, 0);
  if (overlayPanel_) {
    overlayPanel_->syncTimeline(currentSeconds, currentDurationSeconds_);
  }
}

void YouTubePlayerPage::keyPressEvent(QKeyEvent *event) {
  if (handleKeyPress(event)) {
    return;
  }

  QWidget::keyPressEvent(event);
}

void YouTubePlayerPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updatePlaybackSurfaceGeometry();
}

void YouTubePlayerPage::updatePlaybackSurfaceGeometry() {
  if (!videoStage_ || !surfaceFrame_) {
    return;
  }

  const QSize stageSize = videoStage_->size();
  if (stageSize.width() <= 0 || stageSize.height() <= 0) {
    return;
  }

  surfaceFrame_->setFixedSize(stageSize);
}

void YouTubePlayerPage::applyVideoBlur(bool enabled) {
  if (!videoWidget_) {
    return;
  }

  if (enabled) {
    if (!videoBlurEffect_ ||
        videoWidget_->graphicsEffect() != videoBlurEffect_) {
      videoBlurEffect_ = new QGraphicsBlurEffect();
      videoBlurEffect_->setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    }
    videoBlurEffect_->setBlurRadius(16.0);
    videoWidget_->setGraphicsEffect(videoBlurEffect_);
  } else {
    if (videoWidget_->graphicsEffect() == videoBlurEffect_) {
      videoBlurEffect_ = nullptr;
    }
    videoWidget_->setGraphicsEffect(nullptr);
  }
}

void YouTubePlayerPage::refreshChromeStateProperties(bool recommendationsActive,
                                                     bool compactChrome) {
  if (videoStage_) {
    videoStage_->setProperty("recommendations_active", recommendationsActive);
    videoStage_->setProperty("compact_chrome", compactChrome);
    videoStage_->style()->unpolish(videoStage_);
    videoStage_->style()->polish(videoStage_);
    videoStage_->update();
  }

  if (chromeOverlay_) {
    chromeOverlay_->setProperty("recommendations_active",
                                recommendationsActive);
    chromeOverlay_->setProperty("compact_chrome", compactChrome);
    chromeOverlay_->style()->unpolish(chromeOverlay_);
    chromeOverlay_->style()->polish(chromeOverlay_);
    chromeOverlay_->update();
  }

  if (overlayPanel_) {
    overlayPanel_->setProperty("recommendations_active", recommendationsActive);
    overlayPanel_->setProperty("compact_chrome", compactChrome);
    overlayPanel_->style()->unpolish(overlayPanel_);
    overlayPanel_->style()->polish(overlayPanel_);
    overlayPanel_->update();
  }
}

} // namespace GUI
} // namespace AIO
