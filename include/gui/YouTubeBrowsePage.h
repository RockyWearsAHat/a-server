#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>
#include <vector>

#include "streaming/StreamingService.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QTimer;
class QFrame;
class QHBoxLayout;
class QWidget;

namespace AIO {
namespace Streaming {
class YouTubeService;
struct YouTubeContentRail;
struct YouTubeDeviceAuthSession;
} // namespace Streaming

namespace GUI {

class YouTubeBrowsePage : public QWidget {
  Q_OBJECT

public:
  explicit YouTubeBrowsePage(QWidget *parent = nullptr);
  ~YouTubeBrowsePage() override;

signals:
  void homeRequested();
  void videoRequested(const QString &url);

public slots:
  void loadTrending();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  enum class InputMode { Mouse, Nav };

  struct RailModel {
    QString key;
    QString title;
    QString subtitle;
    std::vector<AIO::Streaming::VideoContent> items;
  };

  struct GuideItem {
    QString key;
    QString title;
    QString iconText;
    bool inLibrarySection = false;
  };

  void setupUi();
  void runSearch();
  void refreshHome();
  void requestAdditionalDiscoveryIfNeeded(int targetRailIndex);
  void loadMoreHomeRows();
  void setStatus(const QString &text);
  void setLoadingState(bool loading, const QString &text);
  void updateSectionHeader();
  QString summaryFor(const AIO::Streaming::VideoContent &item) const;
  void setRails(const std::vector<AIO::Streaming::YouTubeContentRail> &rails,
                const QString &heroBody, const QString &accountLabel,
                bool preserveFocus = false);

  void setInputMode(InputMode mode);
  void setFocusedItem(int railIndex, int itemIndex, bool ensureVisible);
  void setFocusToAuthCard(bool ensureVisible);
  void clearHover();

  void rebuildContent();
  void rebuildGuide();
  void rebuildAuthCard();
  void updateHeroSpotlight();
  void updateSidebarState(bool animated);
  void scheduleContentRebuild();
  void refreshLoadedThumbnail(const QString &url);
  void refreshAuthArtwork(const QString &url);
  void toggleLibrarySection();
  void updateFocusStyle();
  void activateFocused();
  void activateGuideItem();
  void ensureFocusedVisible();
  void moveFocus(int dx, int dy);
  void setSearchFocused(bool focused);
  void startDeviceAuth();
  void pollDeviceAuth();
  void performAuthPrimaryAction();
  void signOutYouTube();
  int effectiveItemIndexForRail(int railIndex) const;
  int railIndexForGuideSelection() const;
  int itemsPerRail() const;
  bool hasInteractiveAuthCard() const;
  QString qrImageUrlForSession() const;

  InputMode inputMode_ = InputMode::Mouse;
  bool cursorHidden_ = false;
  bool guideSelected_ = true;
  bool sidebarExpanded_ = true;
  bool librarySectionExpanded_ = true;
  bool authCardSelected_ = false;
  bool authCardHovered_ = false;
  int hoveredGuideIndex_ = -1;
  int hoveredRailIndex_ = -1;
  int hoveredItemIndex_ = -1;
  uint64_t sidebarAnimationEpoch_ = 0;

  QFrame *sidebar_{};
  QLabel *sidebarTitleLabel_{};
  QWidget *guideList_{};
  QVBoxLayout *guideLayout_{};
  QWidget *libraryHeader_{};
  QWidget *topBar_{};
  QPushButton *backButton_{};
  QPushButton *homeButton_{};
  QLabel *titleLabel_{};
  QPushButton *accountButton_{};

  QFrame *heroCard_{};
  QLabel *heroEyebrowLabel_{};
  QLabel *heroTitleLabel_{};
  QLabel *heroBodyLabel_{};
  QLabel *heroPrimaryChip_{};
  QLabel *heroSecondaryChip_{};
  QLabel *heroTertiaryChip_{};
  QFrame *heroSpotlightCard_{};
  QLabel *heroSpotlightEyebrowLabel_{};
  QLabel *heroSpotlightTitleLabel_{};
  QLabel *heroSpotlightMetaLabel_{};

  QLineEdit *searchEdit_{};
  QPushButton *searchButton_{};

  QLabel *statusLabel_{};

  QScrollArea *scroll_{};
  QWidget *contentHost_{};
  QVBoxLayout *contentLayout_{};

  QFrame *authCard_{};
  QLabel *authAvatarLabel_{};
  QLabel *authTitleLabel_{};
  QLabel *authBodyLabel_{};
  QLabel *authCodeLabel_{};
  QLabel *authUrlLabel_{};
  QLabel *authHintLabel_{};
  QLabel *authFooterLabel_{};
  QLabel *authQrLabel_{};
  QPushButton *authPrimaryButton_{};
  QPushButton *authSecondaryButton_{};

  std::vector<RailModel> rails_;
  std::vector<GuideItem> guideItems_;
  std::vector<QWidget *> guideButtons_;
  std::vector<QScrollArea *> railScrolls_;
  std::vector<std::vector<QFrame *>> railTiles_;
  std::vector<int> railSelections_;
  int selectedGuideIndex_ = 0;
  int focusedRailIndex_ = -1;
  int focusedItemIndex_ = 0;
  int contentViewportWidth_ = -1;
  int contentTileWidth_ = -1;
  bool contentRebuildScheduled_ = false;
  int restoreVerticalScrollValue_ = -1;
  std::vector<int> restoreHorizontalScrollValues_;
  bool loadingMoreHome_ = false;
  int homeDiscoveryDepth_ = 0;
  uint64_t requestSerial_ = 0;
  QString currentSectionTitle_ = QStringLiteral("For you");
  QString heroBody_;
  QString accountLabel_;
  QString accountAvatarUrl_;
  AIO::Streaming::YouTubeDeviceAuthSession *authSession_{};
  QTimer *authPollTimer_{};

  AIO::Streaming::YouTubeService *youTube_{};
};

} // namespace GUI
} // namespace AIO
