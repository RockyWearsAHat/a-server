#include "gui/YouTubePlayerPage.h"

#include "gui/ThumbnailCache.h"
#include "gui/youtube/YouTubePlayerOverlay.h"
#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFrame>
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

  topBar_ = new QWidget(this);
  topBar_->setObjectName("aioTopBar");

  auto *barLayout = new QHBoxLayout(topBar_);
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

  reloadButton_ = new QToolButton(topBar_);
  reloadButton_->setText("Reload");
  reloadButton_->setAutoRaise(true);
  reloadButton_->setFocusPolicy(Qt::NoFocus);
  reloadButton_->setProperty("variant", "secondary");

  titleLabel_ = new QLabel("YouTube", topBar_);
  titleLabel_->setProperty("role", "subtitle");

  barLayout->addWidget(backButton_);
  barLayout->addWidget(homeButton_);
  barLayout->addWidget(reloadButton_);
  barLayout->addSpacing(8);
  barLayout->addWidget(titleLabel_);
  barLayout->addStretch();

  videoWidget_ = new QVideoWidget(surfaceFrame_);
  videoWidget_->setObjectName("aioYouTubePlaybackView");
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
  chromeLayout->setContentsMargins(0, 0, 0, 0);
  chromeLayout->setSpacing(0);

  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName("aioYouTubePlayerStatus");
  statusLabel_->setProperty("role", "subtitle");
  statusLabel_->setContentsMargins(18, 8, 18, 8);

  chromeLayout->addWidget(topBar_);
  chromeLayout->addWidget(statusLabel_);
  chromeLayout->addStretch(1);
  setupOverlayUi();
  chromeLayout->addWidget(overlayPanel_);
  stageLayout->addWidget(chromeOverlay_, 0, 0);

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

  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &) { rebuildRecommendations(); });

  updateStatusText("Ready");
  updateOverlayHints();
  updatePlaybackChrome();
}

void YouTubePlayerPage::setupOverlayUi() {
  overlayPanel_ = new QFrame(this);
  overlayPanel_->setObjectName("aioYouTubePlayerOverlay");

  auto *overlayLayout = new QVBoxLayout(overlayPanel_);
  overlayLayout->setContentsMargins(28, 18, 28, 22);
  overlayLayout->setSpacing(12);

  overlayTitleLabel_ = new QLabel("Now playing", overlayPanel_);
  overlayTitleLabel_->setProperty("role", "ytSectionTitle");

  overlayHintLabel_ = new QLabel(overlayPanel_);
  overlayHintLabel_->setProperty("role", "ytSectionMeta");
  overlayHintLabel_->setWordWrap(true);

  currentTimeLabel_ = new QLabel("0:00", overlayPanel_);
  currentTimeLabel_->setProperty("role", "ytSectionMeta");
  durationLabel_ = new QLabel("0:00", overlayPanel_);
  durationLabel_->setProperty("role", "ytSectionMeta");
  timelineSlider_ = new QSlider(Qt::Horizontal, overlayPanel_);
  timelineSlider_->setObjectName("aioYouTubeTimeline");
  timelineSlider_->setRange(0, 1000);
  timelineSlider_->setEnabled(false);

  auto *timelineCard = new QFrame(overlayPanel_);
  timelineCard->setObjectName("aioYouTubeTimelineCard");
  auto *timelineCardLayout = new QHBoxLayout(timelineCard);
  timelineCardLayout->setContentsMargins(12, 8, 12, 8);
  timelineCardLayout->setSpacing(14);
  timelineCardLayout->addWidget(currentTimeLabel_);
  timelineCardLayout->addWidget(timelineSlider_, 1);
  timelineCardLayout->addWidget(durationLabel_);

  auto *recLabel = new QLabel("Recommended next", overlayPanel_);
  recLabel->setProperty("role", "ytSectionTitle");
  recommendationsLabel_ = recLabel;

  recommendationsScroll_ = new QScrollArea(overlayPanel_);
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

  overlayLayout->addWidget(overlayTitleLabel_);
  overlayLayout->addWidget(overlayHintLabel_);
  overlayLayout->addWidget(timelineCard);
  overlayLayout->addWidget(recLabel);
  overlayLayout->addWidget(recommendationsScroll_);
}

void YouTubePlayerPage::setTopBarText(const QString &text) {
  titleLabel_->setText(text);
}

void YouTubePlayerPage::updateStatusText(const QString &text) {
  statusLabel_->setText(text);
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
              return;
            }

            const QString title =
                QString::fromStdString(stream->title).trimmed();
            guard->setTopBarText(
                title.isEmpty() ? QStringLiteral("YouTube")
                                : QStringLiteral("YouTube  |  %1").arg(title));
            if (guard->overlayTitleLabel_) {
              guard->overlayTitleLabel_->setText(
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
  recommendationTiles_.clear();
  selectedRecommendationIndex_ = 0;
  rebuildRecommendations();

  if (!youTube_ || videoId.isEmpty()) {
    updateOverlayHints();
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
          guard->selectedRecommendationIndex_ = 0;
          guard->rebuildRecommendations();
          guard->updateOverlayHints();
        },
        Qt::QueuedConnection);
  }).detach();
}

void YouTubePlayerPage::rebuildRecommendations() {
  if (!recommendationsLayout_) {
    return;
  }

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
    tile->setProperty("selected", i == selectedRecommendationIndex_);
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
      QPixmap px;
      if (ThumbnailCache::instance().tryGet(thumbUrl, &px)) {
        thumb->setPixmap(px.scaled(thumb->size(),
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
  updateRecommendationFocus();
}

void YouTubePlayerPage::updateRecommendationFocus() {
  for (int i = 0; i < static_cast<int>(recommendationTiles_.size()); ++i) {
    auto *tile = recommendationTiles_[i];
    if (!tile) {
      continue;
    }
    tile->setProperty("selected", i == selectedRecommendationIndex_ &&
                                      focusZone_ == FocusZone::Recommendations);
    tile->style()->unpolish(tile);
    tile->style()->polish(tile);
    tile->update();
  }

  if (focusZone_ == FocusZone::Recommendations &&
      selectedRecommendationIndex_ >= 0 &&
      selectedRecommendationIndex_ <
          static_cast<int>(recommendationTiles_.size())) {
    recommendationsScroll_->ensureWidgetVisible(
        recommendationTiles_[selectedRecommendationIndex_], 24, 24);
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
  const bool timelineSelected = (focusZone_ == FocusZone::Timeline);
  timelineSlider_->setProperty("selected", timelineSelected);
  timelineSlider_->style()->unpolish(timelineSlider_);
  timelineSlider_->style()->polish(timelineSlider_);
  timelineSlider_->update();
  updateRecommendationFocus();
  updateOverlayHints();
  updatePlaybackChrome();
}

void YouTubePlayerPage::updateOverlayHints() {
  QString hint;
  switch (focusZone_) {
  case FocusZone::Web:
    hint = QStringLiteral("Press any direction to open playback controls.");
    break;
  case FocusZone::Timeline:
    hint = QStringLiteral("Left or right seeks 10 seconds. Select toggles "
                          "playback. Down opens Up Next.");
    break;
  case FocusZone::Recommendations:
    hint = QStringLiteral("Browse Up Next, press Select to switch videos, or "
                          "Up to return to the timeline.");
    break;
  }
  if (recommendedVideos_.empty() && focusZone_ != FocusZone::Web) {
    hint += QStringLiteral(" Related videos are still loading.");
  }
  overlayHintLabel_->setText(hint);
}

void YouTubePlayerPage::updatePlaybackChrome() {
  const bool showTopChrome = true;
  const bool showOverlay = true;
  const bool showRecommendations = loadFailed_ || !recommendedVideos_.empty() ||
                                   focusZone_ == FocusZone::Recommendations;

  topBar_->setVisible(showTopChrome);
  statusLabel_->setVisible(showTopChrome);
  overlayPanel_->setVisible(showOverlay);
  if (recommendationsLabel_) {
    recommendationsLabel_->setVisible(showRecommendations);
  }
  recommendationsScroll_->setVisible(showRecommendations);
}

bool YouTubePlayerPage::handleKeyPress(QKeyEvent *event) {
  if (!event) {
    return false;
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
    if (focusZone_ == FocusZone::Timeline) {
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
  timelineSlider_->setEnabled(currentDurationSeconds_ > 0);
  timelineSlider_->setRange(0, std::max(currentDurationSeconds_, 1));
  timelineSlider_->setValue(
      std::clamp(currentSeconds, 0, std::max(currentDurationSeconds_, 1)));
  currentTimeLabel_->setText(formatTime(std::max(currentSeconds, 0)));
  durationLabel_->setText(formatTime(std::max(currentDurationSeconds_, 0)));
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

  const int horizontalPadding = std::clamp(stageSize.width() / 24, 24, 80);
  const int verticalPadding = std::clamp(stageSize.height() / 12, 20, 72);
  const int maxWidth = std::max(320, stageSize.width() - horizontalPadding * 2);
  const int maxHeight = std::max(180, stageSize.height() - verticalPadding * 2);

  int width = maxWidth;
  int height = (width * 9) / 16;
  if (height > maxHeight) {
    height = maxHeight;
    width = (height * 16) / 9;
  }

  surfaceFrame_->setFixedSize(width, height);
}

} // namespace GUI
} // namespace AIO
