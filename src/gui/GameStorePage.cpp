#include "gui/GameStorePage.h"
#include "gui/SteamAuthDialog.h"
#include "gui/ThumbnailCache.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace AIO::GUI {

static constexpr int kCatalogPageSize = 48;

static QString formatPrice(int cents) {
  if (cents <= 0)
    return QStringLiteral("Free to Play");
  const int dollars = cents / 100;
  const int rem = cents % 100;
  return QStringLiteral("$%1.%2").arg(dollars).arg(rem, 2, 10, QChar('0'));
}

static QString libraryStatusText(const StoreGame &game) {
  if (game.isRomGame)
    return QStringLiteral("Local Library");
  return game.isInstalled ? QStringLiteral("Installed on this device")
                          : QStringLiteral("Steam library entry");
}

static QColor storeCardSurfaceColor() {
  return QColor(QStringLiteral("#121212"));
}

static QColor storeSkeletonColor() { return QColor(QStringLiteral("#2a2a2a")); }

static QColor categoryAccentColor(const StoreGame &game) {
  if (game.category == QStringLiteral("GBA"))
    return QColor(QStringLiteral("#9c8cff"));
  if (game.category == QStringLiteral("PS1"))
    return QColor(QStringLiteral("#64b5f6"));
  if (game.category == QStringLiteral("Switch"))
    return QColor(QStringLiteral("#ff6b6b"));
  return game.coverColor.isValid() ? game.coverColor
                                   : QColor(QStringLiteral("#64b5f6"));
}

static void paintFallbackThumbnail(QPainter &p, const QRectF &rect,
                                   const StoreGame &game, qreal radius) {
  const QColor accent = categoryAccentColor(game);

  QPainterPath path;
  path.addRoundedRect(rect, radius, radius);
  QLinearGradient gradient(rect.left(), rect.top(), rect.right(),
                           rect.bottom());
  gradient.setColorAt(0.0, accent.darker(210));
  gradient.setColorAt(0.55, QColor(QStringLiteral("#1a1a1a")));
  gradient.setColorAt(1.0, QColor(QStringLiteral("#0a0a0a")));
  p.fillPath(path, gradient);

  QLinearGradient sheen(rect.left(), rect.top(), rect.left(), rect.bottom());
  sheen.setColorAt(0.0, QColor(255, 255, 255, 24));
  sheen.setColorAt(0.45, QColor(255, 255, 255, 0));
  sheen.setColorAt(1.0, QColor(0, 0, 0, 72));
  p.fillPath(path, sheen);

  p.setPen(QPen(QColor(255, 255, 255, 18), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);

  QFont badgeFont(QStringLiteral("Noto Sans"));
  badgeFont.setPixelSize(12);
  badgeFont.setWeight(QFont::DemiBold);
  p.setFont(badgeFont);
  const QRectF badgeRect(rect.left() + 16.0, rect.top() + 16.0, 92.0, 24.0);
  QPainterPath badgePath;
  badgePath.addRoundedRect(badgeRect, 6.0, 6.0);
  p.fillPath(badgePath,
             QColor(accent.red(), accent.green(), accent.blue(), 56));
  p.setPen(accent.lighter(135));
  p.drawText(badgeRect, Qt::AlignCenter,
             game.category.isEmpty() ? QStringLiteral("Store") : game.category);

  QFont initialFont(QStringLiteral("Noto Sans"));
  initialFont.setPixelSize(48);
  initialFont.setWeight(QFont::Bold);
  p.setFont(initialFont);
  p.setPen(QColor(255, 255, 255, 24));
  p.drawText(rect.toRect().adjusted(0, 0, 0, -32), Qt::AlignCenter,
             game.title.isEmpty() ? QStringLiteral("?")
                                  : game.title.left(1).toUpper());
}

static void paintSkeletonThumbnail(QPainter &p, const QRectF &rect,
                                   qreal radius) {
  QPainterPath skeletonPath;
  skeletonPath.addRoundedRect(rect, radius, radius);
  p.fillPath(skeletonPath, storeSkeletonColor());
  p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawPath(skeletonPath);

  QLinearGradient shimmer(rect.left(), rect.top(), rect.right(), rect.top());
  shimmer.setColorAt(0.0, QColor(255, 255, 255, 0));
  shimmer.setColorAt(0.30, QColor(255, 255, 255, 0));
  shimmer.setColorAt(0.50, QColor(255, 255, 255, 28));
  shimmer.setColorAt(0.70, QColor(255, 255, 255, 0));
  shimmer.setColorAt(1.0, QColor(255, 255, 255, 0));
  p.fillPath(skeletonPath, shimmer);

  const QRectF topBar(rect.left() + 16.0, rect.top() + 18.0,
                      rect.width() * 0.46, 14.0);
  const QRectF midBar(rect.left() + 16.0, rect.center().y() - 8.0,
                      rect.width() * 0.62, 12.0);
  const QRectF lowBar(rect.left() + 16.0, rect.bottom() - 28.0,
                      rect.width() * 0.40, 12.0);
  for (const QRectF &bar : {topBar, midBar, lowBar}) {
    QPainterPath barPath;
    barPath.addRoundedRect(bar, 6.0, 6.0);
    p.fillPath(barPath, QColor(255, 255, 255, 30));
  }
}

// GameCard: private to this translation unit
class GameCard : public QFrame {
public:
  explicit GameCard(const StoreGame &game, QWidget *parent = nullptr)
      : QFrame(parent), game_(game) {
    setObjectName(QStringLiteral("aioGameCard"));
    setFixedHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    titleLabel_ = new QLabel(game.title, this);
    titleLabel_->setObjectName(QStringLiteral("aioStoreCardTitle"));
    titleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel_->setMinimumWidth(0);
    QFont tf;
    tf.setPixelSize(16);
    tf.setWeight(QFont::Bold);
    titleLabel_->setFont(tf);
    const QString priceStr =
        game.hideCommerce
            ? libraryStatusText(game)
            : (game.isRomGame
                   ? QStringLiteral("Ready to launch")
                   : ((game.isOnSale && game.discountPercent > 0)
                          ? QStringLiteral("-%1% off").arg(game.discountPercent)
                          : formatPrice(game.priceUsdCents)));
    pubLabel_ = new QLabel(priceStr, this);
    pubLabel_->setObjectName(QStringLiteral("aioStoreCardMeta"));
    pubLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    pubLabel_->setMinimumWidth(0);
    QFont pf;
    pf.setPixelSize(13);
    pubLabel_->setFont(pf);
    pubLabel_->setProperty("aio_tone",
                           (game.hideCommerce || game.isRomGame)
                               ? QStringLiteral("library")
                               : ((game.isOnSale && game.discountPercent > 0)
                                      ? QStringLiteral("sale")
                                      : QStringLiteral("default")));
    lay->addStretch(1);
    lay->addWidget(titleLabel_);
    lay->addWidget(pubLabel_);

    coverUrls_ = game.coverArtUrls;
    if (coverUrls_.isEmpty() && !game.coverArtUrl.isEmpty())
      coverUrls_.append(game.coverArtUrl);
    if (!coverUrls_.isEmpty()) {
      QObject::connect(
          &ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &url) {
            const int idx = coverUrls_.indexOf(url);
            if (idx < 0)
              return;
            if (ThumbnailCache::instance().tryGet(url, &coverPixmap_)) {
              coverUrl_ = url;
              requestedCoverIndex_ = idx;
              update();
            }
          });
      QObject::connect(&ThumbnailCache::instance(),
                       &ThumbnailCache::thumbnailFailed, this,
                       [this](const QString &url) {
                         if (requestedCoverIndex_ < 0 ||
                             requestedCoverIndex_ >= coverUrls_.size())
                           return;
                         if (coverUrls_.at(requestedCoverIndex_) != url)
                           return;
                         requestCover(requestedCoverIndex_ + 1);
                       });
      requestCover(0);
    }
  }

  void setSelected(bool sel) {
    if (property("aio_selected").toBool() == sel)
      return;
    setProperty("aio_selected", sel);
    style()->unpolish(this);
    style()->polish(this);
    update();
  }

  const StoreGame &game() const { return game_; }
  QSize minimumSizeHint() const override { return QSize(0, 260); }
  QSize sizeHint() const override { return QSize(280, 260); }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());
    const QRectF r(rect());
    const qreal radius = 16.0;
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, storeCardSurfaceColor());

    if (!coverPixmap_.isNull()) {
      const QPixmap scaled = coverPixmap_.scaled(
          size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      p.save();
      p.setClipPath(path);
      const QRect target = rect();
      const QRect source((scaled.width() - target.width()) / 2,
                         (scaled.height() - target.height()) / 2,
                         target.width(), target.height());
      p.drawPixmap(target, scaled, source);
      p.restore();

      QLinearGradient scrim(r.left(), r.top(), r.left(), r.bottom());
      scrim.setColorAt(0.0, QColor(0, 0, 0, 20));
      scrim.setColorAt(0.40, QColor(0, 0, 0, 0));
      scrim.setColorAt(0.65, QColor(0, 0, 0, 140));
      scrim.setColorAt(1.0, QColor(0, 0, 0, 230));
      p.fillPath(path, scrim);
    }

    if (coverPixmap_.isNull()) {
      const QRectF fallbackRect(r.left() + 14.0, r.top() + 14.0,
                                r.width() - 28.0, r.height() - 80.0);
      paintFallbackThumbnail(p, fallbackRect, game_, 12.0);
    }

    if (game_.isInstalled || game_.isOnSale ||
        (game_.isOwned && !game_.isInstalled)) {
      const QString statusText =
          game_.isInstalled
              ? (game_.isRomGame ? QStringLiteral("Installed")
                                 : QStringLiteral("In Library"))
              : (game_.isOwned
                     ? QStringLiteral("Owned")
                     : QStringLiteral("-%1%").arg(game_.discountPercent));
      QFont badgeFont;
      badgeFont.setPixelSize(12);
      badgeFont.setWeight(QFont::Bold);
      p.setFont(badgeFont);
      const QFontMetrics badgeMetrics(badgeFont);
      const int badgeWidth = badgeMetrics.horizontalAdvance(statusText) + 20;
      const QRectF badgeRect(r.right() - badgeWidth - 12.0, r.top() + 12.0,
                             badgeWidth, 24.0);
      QPainterPath badgePath;
      badgePath.addRoundedRect(badgeRect, 6, 6);
      p.fillPath(badgePath, game_.isInstalled
                                ? QColor(63, 185, 80, 220)
                                : (game_.isOwned ? QColor(100, 181, 246, 220)
                                                 : QColor(212, 168, 32, 224)));
      p.setPen(QColor(QStringLiteral("#0a0a0a")));
      p.drawText(badgeRect, Qt::AlignCenter, statusText);
    }

    if (property("aio_selected").toBool()) {
      QPainterPath glow;
      glow.addRoundedRect(r.adjusted(2.0, 2.0, -2.0, -2.0), radius - 2.0,
                          radius - 2.0);
      p.setPen(QPen(QColor(255, 255, 255, 56), 16.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(glow);

      QPainterPath ring;
      ring.addRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), radius - 1.0,
                          radius - 1.0);
      p.setPen(QPen(QColor(255, 255, 255, 255), 3.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(ring);
    }

    {
      const QString srcText = game_.category.isEmpty()
                                  ? (game_.isRomGame ? QStringLiteral("Library")
                                                     : QStringLiteral("Store"))
                                  : game_.category;
      QFont srcFont;
      srcFont.setPixelSize(12);
      srcFont.setWeight(QFont::DemiBold);
      p.setFont(srcFont);
      const QFontMetrics sfm(srcFont);
      const int sw = sfm.horizontalAdvance(srcText) + 16;
      const QRectF srcRect(r.left() + 12, r.bottom() - 36.0, sw, 24.0);
      QPainterPath srcPath;
      srcPath.addRoundedRect(srcRect, 6, 6);
      const QColor accent = categoryAccentColor(game_);
      const QColor bg = QColor(10, 10, 10, 176);
      const QColor fg = QColor(QStringLiteral("#f0f0f0"));
      p.fillPath(srcPath, bg);
      p.setPen(fg);
      p.drawText(srcRect, Qt::AlignCenter, srcText);
    }
  }

private:
  void requestCover(int startIndex) {
    for (int i = qMax(0, startIndex); i < coverUrls_.size(); ++i) {
      QPixmap candidate;
      if (ThumbnailCache::instance().tryGet(coverUrls_.at(i), &candidate)) {
        coverPixmap_ = candidate;
        coverUrl_ = coverUrls_.at(i);
        requestedCoverIndex_ = i;
        update();
        return;
      }

      requestedCoverIndex_ = i;
      ThumbnailCache::instance().request(coverUrls_.at(i));
      return;
    }
    requestedCoverIndex_ = coverUrls_.size();
  }

  StoreGame game_;
  QString coverUrl_;
  QStringList coverUrls_;
  QPixmap coverPixmap_;
  int requestedCoverIndex_ = -1;
  QLabel *titleLabel_ = nullptr;
  QLabel *pubLabel_ = nullptr;
};

// GameStorePage
GameStorePage::GameStorePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("aioGameStorePage"));
  setFocusPolicy(Qt::StrongFocus);
  categories_.append(QStringLiteral("All"));
  categories_.append(QStringLiteral("Top Sellers"));
  categories_.append(QStringLiteral("New Releases"));
  categories_.append(QStringLiteral("On Sale"));
  categories_.append(QStringLiteral("Coming Soon"));
  categories_.append(QStringLiteral("My Library"));
  categories_.append(QStringLiteral("Account"));
  setupUi();
}

void GameStorePage::setSteamService(SteamService *service) {
  steamService_ = service;
  connect(steamService_, &SteamService::gamesPageReady, this,
          &GameStorePage::onSteamGamesPageReady);
  connect(steamService_, &SteamService::fetchError, this,
          &GameStorePage::showSteamError);
  connect(steamService_, &SteamService::ownedLibraryReady, this,
          &GameStorePage::onOwnedLibraryReady);
  connect(steamService_, &SteamService::authError, this,
          &GameStorePage::onSteamAuthError);
  requestCatalogIfNeeded();
  updateAccountStatus();
  // Auto-fetch owned library if credentials are already stored.
  // Do NOT set ownedLibraryFetched_ here — if the server isn't up yet the
  // fetch will fail and we want My Library navigation to retry automatically.
  if (steamService_->hasSteamId()) {
    steamService_->fetchOwnedLibraryXml();
  }
}

void GameStorePage::requestCatalogIfNeeded() {
  if (!steamService_ || catalogRequested_)
    return;
  catalogRequested_ = true;
  requestCatalogPage(true);
}

QString GameStorePage::activeCatalogCategoryKey() const {
  if (activeCategoryIndex_ < 0 || activeCategoryIndex_ >= categories_.size())
    return QStringLiteral("all");

  const QString category = categories_.at(activeCategoryIndex_);
  if (category == QStringLiteral("Top Sellers"))
    return QStringLiteral("top-sellers");
  if (category == QStringLiteral("New Releases"))
    return QStringLiteral("new-releases");
  if (category == QStringLiteral("On Sale"))
    return QStringLiteral("on-sale");
  if (category == QStringLiteral("Coming Soon"))
    return QStringLiteral("coming-soon");
  return QStringLiteral("all");
}

void GameStorePage::requestCatalogPage(bool reset) {
  if (!steamService_)
    return;

  const bool libraryCategory =
      activeCategoryIndex_ >= 0 && activeCategoryIndex_ < categories_.size() &&
      (categories_.at(activeCategoryIndex_) == QStringLiteral("My Library") ||
       categories_.at(activeCategoryIndex_) == QStringLiteral("Account"));
  if (libraryCategory) {
    catalogLoading_ = false;
    catalogRequestInFlight_ = false;
    catalogHasMore_ = false;
    return;
  }

  const QString query = searchQuery_.trimmed();
  const QString categoryKey = activeCatalogCategoryKey();
  if (!reset && (!catalogHasMore_ || catalogRequestInFlight_))
    return;

  if (reset) {
    catalogCategoryKey_ = categoryKey;
    catalogQuery_ = query;
    catalogStart_ = 0;
    catalogTotalCount_ = 0;
    catalogHasMore_ = false;
    catalogLoading_ = true;
    catalogRequestInFlight_ = true;
    allGames_.clear();
    steamGames_.clear();
    gridFocusRow_ = 0;
    gridFocusCol_ = 0;
    errorShown_ = false;
    errorMessage_.clear();
    applyActiveCategoryFilter();
    rebuildGrid();
    resetGridViewport();
  } else {
    catalogLoading_ = true;
    catalogRequestInFlight_ = true;
  }

  steamService_->fetchCatalogPage(catalogCategoryKey_, catalogQuery_,
                                  catalogStart_, kCatalogPageSize);
}

void GameStorePage::applyActiveCategoryFilter() {
  if (categories_.isEmpty()) {
    filteredGames_.clear();
    updateShelfHeader();
    return;
  }

  activeCategoryIndex_ =
      qBound(0, activeCategoryIndex_, categories_.size() - 1);
  const QString &cat = categories_[activeCategoryIndex_];
  filteredGames_.clear();

  auto appendVisibleGame = [this](StoreGame game, bool libraryEntry) {
    game.hideCommerce = libraryEntry && !game.isRomGame;
    if (!matchesLibraryFilter(game))
      return;
    if (!matchesSearch(game))
      return;
    filteredGames_.append(game);
  };

  if (cat == QStringLiteral("My Library")) {
    if (accountPage_)
      accountPage_->hide();
    if (shelfHeader_)
      shelfHeader_->show();
    if (controlsBar_)
      controlsBar_->show();
    if (contentArea_)
      contentArea_->show();
    libraryModeActive_ = true;
    for (const StoreGame &game : libraryGames_)
      appendVisibleGame(game, true);
  } else if (cat == QStringLiteral("Account")) {
    // Account page replaces catalog section — managed below
    libraryModeActive_ = false;
    if (accountPage_)
      accountPage_->show();
    if (shelfHeader_)
      shelfHeader_->hide();
    if (controlsBar_)
      controlsBar_->hide();
    if (contentArea_)
      contentArea_->hide();
    signinFocus_ = 0;
    if (signinStatusLabel_)
      signinStatusLabel_->hide();
    // Set initial button focus on the Sign In button (index 0)
    if (accountPage_) {
      const auto btns = accountPage_->findChildren<QPushButton *>(
          QStringLiteral("aioStoreAccountBtn"));
      for (int i = 0; i < btns.size(); ++i) {
        btns[i]->setProperty("aio_focused", (i == 0) ? "true" : "false");
        btns[i]->style()->unpolish(btns[i]);
        btns[i]->style()->polish(btns[i]);
      }
    }
    focusArea_ = FocusArea::SignIn;
    return;
  } else {
    if (accountPage_)
      accountPage_->hide();
    if (shelfHeader_)
      shelfHeader_->show();
    if (controlsBar_)
      controlsBar_->show();
    if (contentArea_)
      contentArea_->show();
    libraryModeActive_ = false;
    for (const StoreGame &game : allGames_)
      appendVisibleGame(game, false);
  }

  updateShelfHeader();
  updateSearchControls();
  updateLibraryFilterFocus();
}

bool GameStorePage::matchesSearch(const StoreGame &game) const {
  const QString query = searchQuery_.trimmed();
  if (query.isEmpty())
    return true;

  const QString haystack =
      QStringLiteral("%1 %2 %3")
          .arg(game.title, game.category,
               game.isInstalled ? QStringLiteral("installed")
                                : QStringLiteral("installable"));
  return haystack.contains(query, Qt::CaseInsensitive);
}

bool GameStorePage::matchesLibraryFilter(const StoreGame &game) const {
  if (!libraryModeActive_)
    return true;

  switch (libraryFilter_) {
  case LibraryFilter::All:
    return true;
  case LibraryFilter::Steam:
    return !game.isRomGame;
  case LibraryFilter::Local:
    return game.isRomGame;
  case LibraryFilter::Unsupported:
    return game.category == QStringLiteral("Switch");
  }

  return true;
}

void GameStorePage::updateSearchControls() {
  if (!searchLabel_ || !searchValue_)
    return;

  searchLabel_->setText(libraryModeActive_ ? QStringLiteral("Search Library")
                                           : QStringLiteral("Search Steam"));
  searchValue_->setText(searchQuery_.trimmed().isEmpty()
                            ? QStringLiteral("Search titles, genres, and deals")
                            : searchQuery_.trimmed());
  searchValue_->setProperty(
      "aio_active", searchQuery_.trimmed().isEmpty() ? "false" : "true");
  searchValue_->style()->unpolish(searchValue_);
  searchValue_->style()->polish(searchValue_);
}

void GameStorePage::updateLibraryFilterFocus() {
  if (!libraryFilterBar_)
    return;

  libraryFilterBar_->setVisible(libraryModeActive_);
  for (int i = 0; i < libraryFilterLabels_.size(); ++i) {
    const bool active = i == static_cast<int>(libraryFilter_);
    const bool focused =
        focusArea_ == FocusArea::LibraryFilters && i == libraryFilterFocus_;
    libraryFilterLabels_[i]->setProperty("active", active ? "true" : "false");
    libraryFilterLabels_[i]->setProperty("focused", focused ? "true" : "false");
    libraryFilterLabels_[i]->style()->unpolish(libraryFilterLabels_[i]);
    libraryFilterLabels_[i]->style()->polish(libraryFilterLabels_[i]);
  }
}

bool GameStorePage::handleSearchKey(QKeyEvent *event) {
  if (focusArea_ == FocusArea::Detail)
    return false;

  const QString typedText = event->text();
  const bool plainTextInput =
      typedText.size() == 1 && typedText.front().isPrint() &&
      !typedText.front().isSpace() &&
      !(event->modifiers() &
        (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
  if (plainTextInput) {
    searchQuery_.append(typedText);
    if (libraryModeActive_)
      applyActiveCategoryFilter();
    else
      requestCatalogPage(true);
    rebuildGrid();
    event->accept();
    return true;
  }

  if (event->key() == Qt::Key_Backspace && !searchQuery_.isEmpty()) {
    searchQuery_.chop(1);
    if (libraryModeActive_)
      applyActiveCategoryFilter();
    else
      requestCatalogPage(true);
    rebuildGrid();
    event->accept();
    return true;
  }

  return false;
}

void GameStorePage::updateShelfHeader() {
  if (!shelfEyebrow_ || !shelfTitle_ || !shelfSummary_)
    return;

  const QString activeCategory =
      (activeCategoryIndex_ >= 0 && activeCategoryIndex_ < categories_.size())
          ? categories_[activeCategoryIndex_]
          : QStringLiteral("All");
  const bool showingLibrary = activeCategory == QStringLiteral("My Library") ||
                              (activeCategory == QStringLiteral("All") &&
                               allGames_.isEmpty() && !libraryGames_.isEmpty());
  shelfEyebrow_->setText(showingLibrary ? QStringLiteral("YOUR COLLECTION")
                                        : QStringLiteral("DISCOVER"));
  if (showingLibrary) {
    switch (libraryFilter_) {
    case LibraryFilter::All:
      shelfTitle_->setText(QStringLiteral("Installed Library"));
      break;
    case LibraryFilter::Steam:
      shelfTitle_->setText(QStringLiteral("Steam Titles"));
      break;
    case LibraryFilter::Local:
      shelfTitle_->setText(QStringLiteral("Local Library"));
      break;
    case LibraryFilter::Unsupported:
      shelfTitle_->setText(QStringLiteral("Unsupported Entries"));
      break;
    }
  } else {
    shelfTitle_->setText(activeCategory == QStringLiteral("All")
                             ? QStringLiteral("Discover Something New")
                             : activeCategory);
  }

  if (catalogLoading_ && allGames_.isEmpty()) {
    shelfSummary_->setText(QStringLiteral(
        "Fetching live Steam results, pricing, and artwork for this shelf..."));
  } else if (errorShown_ && filteredGames_.isEmpty()) {
    shelfSummary_->setText(
        errorMessage_.isEmpty()
            ? QStringLiteral("The store is unavailable right now.")
            : errorMessage_);
  } else {
    const int totalCount = filteredGames_.size();
    if (showingLibrary) {
      const char *filterNames[] = {"owned entries", "Steam titles",
                                   "local games", "unsupported entries"};
      const int filterIdx = static_cast<int>(libraryFilter_);
      const QString filterName = (filterIdx >= 0 && filterIdx < 4)
                                     ? QString::fromUtf8(filterNames[filterIdx])
                                     : QStringLiteral("entries");
      shelfSummary_->setText(
          searchQuery_.trimmed().isEmpty()
              ? QStringLiteral("Browse %1 %2 from your installed Steam titles "
                               "and local ROM library.")
                    .arg(totalCount)
                    .arg(filterName)
              : QStringLiteral("%1 results for \"%2\" in %3.")
                    .arg(totalCount)
                    .arg(searchQuery_.trimmed(), filterName));
    } else if (activeCategory == QStringLiteral("All")) {
      shelfSummary_->setText(
          searchQuery_.trimmed().isEmpty()
              ? QStringLiteral("Showing %1 of %2 live Steam titles.")
                    .arg(totalCount)
                    .arg(qMax(totalCount, catalogTotalCount_))
              : QStringLiteral("%1 results for \"%2\".")
                    .arg(totalCount)
                    .arg(searchQuery_.trimmed()));
    } else {
      shelfSummary_->setText(
          searchQuery_.trimmed().isEmpty()
              ? QStringLiteral("Showing %1 of %2 titles in %3.")
                    .arg(totalCount)
                    .arg(qMax(totalCount, catalogTotalCount_))
                    .arg(activeCategory)
              : QStringLiteral("%1 %2 results for \"%3\".")
                    .arg(totalCount)
                    .arg(activeCategory)
                    .arg(searchQuery_.trimmed()));
    }
  }
}

void GameStorePage::onSteamGamesPageReady(
    const QList<AIO::GUI::SteamGame> &games, int start, int totalCount,
    bool hasMore, const QString &category, const QString &query) {
  if (category != catalogCategoryKey_ || query.trimmed() != catalogQuery_) {
    return;
  }

  catalogLoading_ = false;
  catalogRequestInFlight_ = false;
  catalogHasMore_ = hasMore;
  catalogTotalCount_ = totalCount;
  errorShown_ = false;
  errorMessage_.clear();

  if (start == 0) {
    steamGames_.clear();
    allGames_.clear();
  }

  QSet<QString> seenIds;
  for (const StoreGame &existing : allGames_)
    seenIds.insert(existing.id);

  const QList<QColor> coverPalette = {
      QColor(100, 181, 246), QColor(156, 140, 255), QColor(255, 107, 107),
      QColor(63, 185, 80),   QColor(251, 188, 4),   QColor(255, 138, 76),
      QColor(38, 166, 154),  QColor(171, 71, 188)};
  for (int i = 0; i < games.size(); ++i) {
    const SteamGame &sg = games.at(i);
    const QString storeId = QString::number(sg.appId);
    if (storeId.isEmpty() || seenIds.contains(storeId))
      continue;

    seenIds.insert(storeId);
    steamGames_.append(sg);

    StoreGame g;
    g.id = storeId;
    g.title = sg.name;
    g.category = sg.category;
    g.coverArtUrl = sg.coverArtUrl;
    g.coverArtUrls = sg.coverArtUrls;
    g.coverColor = coverPalette[(allGames_.size() + i) % coverPalette.size()];
    g.isInstalled = sg.isInstalled;
    g.isOwned = ownedAppIds_.contains(sg.appId);
    g.priceUsdCents = sg.priceUsdCents;
    g.discountPercent = sg.discountPercent;
    g.isOnSale = sg.isOnSale;
    g.sourceLabel = QStringLiteral("STORE");
    allGames_.append(g);
  }

  catalogStart_ = allGames_.size();
  scanLibrary();
  applyActiveCategoryFilter();
  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
}

void GameStorePage::showSteamError(const QString &msg) {
  catalogLoading_ = false;
  catalogRequestInFlight_ = false;
  errorShown_ = true;
  errorMessage_ = msg;
  scanLibrary();
  applyActiveCategoryFilter();
  rebuildGrid();
  resetGridViewport();
}

void GameStorePage::loadCatalog() {
  // Static catalog loading removed. Data now comes from SteamService.
  // This method is retained for API compatibility.
}

int GameStorePage::computeGridCols() const {
  const int available = width() - 96;      // grid side margins
  const int cols = (available + 20) / 300; // min 280px card + 20px gap
  return qBound(2, cols, 6);
}

void GameStorePage::scanLibrary() {
  libraryGames_.clear();

  QSettings settings("AIOServer", "GBAEmulator");
  const QString romDir =
      settings.value("romDirectory", QDir::homePath()).toString();
  const QDir dir(romDir);
  if (dir.exists()) {
    QMap<QString, StoreGame> uniqueGames;
    const QStringList filters = {"*.gba", "*.bin", "*.cue", "*.iso", "*.img",
                                 "*.xci", "*.nsp", "*.nso", "*.nro"};
    QDirIterator it(romDir, filters, QDir::Files, QDirIterator::Subdirectories);
    const QList<QColor> palette = {QColor(100, 181, 246), QColor(156, 140, 255),
                                   QColor(255, 107, 107), QColor(63, 185, 80),
                                   QColor(251, 188, 4),   QColor(255, 138, 76)};
    int idx = 0;
    while (it.hasNext()) {
      it.next();
      const QFileInfo fi = it.fileInfo();
      const QString ext = fi.suffix().toLower();
      StoreGame g;
      g.id = fi.absoluteFilePath();
      g.title = fi.completeBaseName();
      g.isInstalled = true;
      g.isRomGame = true;
      g.coverColor = palette[idx % palette.size()];
      if (ext == "gba") {
        g.category = QStringLiteral("GBA");
      } else if (ext == "bin" || ext == "cue" || ext == "iso" || ext == "img") {
        g.category = QStringLiteral("PS1");
      } else if (ext == "xci" || ext == "nsp" || ext == "nso" || ext == "nro") {
        g.category = QStringLiteral("Switch");
      } else {
        continue;
      }

      const QString key =
          QStringLiteral("%1::%2").arg(g.category, g.title.trimmed().toLower());
      const auto existing = uniqueGames.constFind(key);
      auto extensionRank = [](const QString &path) {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QStringLiteral("cue"))
          return 5;
        if (ext == QStringLiteral("iso"))
          return 4;
        if (ext == QStringLiteral("img"))
          return 3;
        if (ext == QStringLiteral("bin"))
          return 2;
        return 1;
      };
      if (existing == uniqueGames.cend() ||
          extensionRank(existing->id) < extensionRank(g.id)) {
        uniqueGames.insert(key, g);
      }
      ++idx;
    }
    libraryGames_ = uniqueGames.values().toVector();
    std::sort(libraryGames_.begin(), libraryGames_.end(),
              [](const StoreGame &a, const StoreGame &b) {
                return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
              });
  }

  for (const StoreGame &sg : allGames_) {
    if (sg.isInstalled)
      libraryGames_.append(sg);
  }

  // Add owned-but-not-installed Steam games from the personal library
  // Include games not already covered by allGames_ (not in the current catalog
  // page)
  QSet<QString> existingIds;
  for (const StoreGame &g : libraryGames_)
    existingIds.insert(g.id);

  const QList<QColor> ownedPalette = {
      QColor(100, 181, 246), QColor(156, 140, 255), QColor(255, 107, 107),
      QColor(63, 185, 80),   QColor(251, 188, 4),   QColor(255, 138, 76)};
  int ownedIdx = 0;
  for (const SteamGame &sg : ownedSteamGames_) {
    const QString id = QString::number(sg.appId);
    if (existingIds.contains(id))
      continue;
    StoreGame g;
    g.id = id;
    g.title = sg.name;
    g.coverArtUrl = sg.coverArtUrl;
    g.coverArtUrls = sg.coverArtUrls;
    g.isInstalled = sg.isInstalled;
    g.isOwned = true;
    g.coverColor = ownedPalette[ownedIdx % ownedPalette.size()];
    g.sourceLabel = QStringLiteral("OWNED");
    existingIds.insert(id);
    libraryGames_.append(g);
    ++ownedIdx;
  }

  std::sort(libraryGames_.begin(), libraryGames_.end(),
            [](const StoreGame &a, const StoreGame &b) {
              return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
}

void GameStorePage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  const int newCols = computeGridCols();
  if (newCols != kGridCols)
    rebuildGrid();
  updateDetailOverlayLayout();
}

void GameStorePage::setupUi() {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  headerBar_ = new QWidget(this);
  headerBar_->setObjectName(QStringLiteral("aioStoreHeader"));
  auto *hdrLay = new QHBoxLayout(headerBar_);
  hdrLay->setContentsMargins(48, 10, 48, 10);
  hdrLay->setSpacing(16);
  auto *iconLbl = new QLabel(headerBar_);
  iconLbl->setFixedSize(36, 36);
  {
    QPixmap pm(36, 36);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath bag;
    bag.addRoundedRect(QRectF(4, 14, 28, 20), 4, 4);
    p.fillPath(bag, QColor(212, 156, 28, 200));
    p.setPen(QPen(QColor(212, 156, 28), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(10, 4, 16, 16), 0, 180 * 16);
    iconLbl->setPixmap(pm);
  }
  hdrLay->addWidget(iconLbl);
  auto *titleStack = new QVBoxLayout();
  titleStack->setSpacing(2);
  auto *storeTitle = new QLabel(QStringLiteral("Game Store"), headerBar_);
  storeTitle->setObjectName(QStringLiteral("aioStoreTitle"));
  titleStack->addWidget(storeTitle);
  auto *storeSub =
      new QLabel(QStringLiteral("Sign in to Steam, buy games, trigger "
                                "installs, and launch from one shell"),
                 headerBar_);
  storeSub->setObjectName(QStringLiteral("aioStoreSubtitle"));
  titleStack->addWidget(storeSub);
  hdrLay->addLayout(titleStack);
  hdrLay->addStretch();
  accountStatusLabel_ =
      new QLabel(QStringLiteral("Steam: Not signed in"), headerBar_);
  accountStatusLabel_->setObjectName(QStringLiteral("aioStoreAccountStatus"));
  hdrLay->addWidget(accountStatusLabel_);
  auto *backHint =
      new QLabel(QStringLiteral("Esc / B  Return home"), headerBar_);
  backHint->setObjectName(QStringLiteral("aioStoreBackHint"));
  hdrLay->addWidget(backHint);
  outer->addWidget(headerBar_);

  tabBar_ = new QWidget(this);
  tabBar_->setObjectName(QStringLiteral("aioStoreTabBar"));
  auto *tabLay = new QHBoxLayout(tabBar_);
  tabLay->setContentsMargins(48, 8, 48, 8);
  tabLay->setSpacing(8);
  for (const QString &cat : categories_) {
    auto *lbl = new QLabel(cat, tabBar_);
    lbl->setObjectName(QStringLiteral("aioStoreTabLabel"));
    lbl->setAlignment(Qt::AlignCenter);
    tabLay->addWidget(lbl);
    tabLabels_.append(lbl);
  }
  tabLay->addStretch();
  outer->addWidget(tabBar_);

  shelfHeader_ = new QWidget(this);
  shelfHeader_->setObjectName(QStringLiteral("aioStoreShelfHeader"));
  auto *shelfLay = new QVBoxLayout(shelfHeader_);
  shelfLay->setContentsMargins(48, 8, 48, 4);
  shelfLay->setSpacing(2);
  shelfEyebrow_ = new QLabel(QStringLiteral("DISCOVER"), shelfHeader_);
  shelfEyebrow_->setObjectName(QStringLiteral("aioStoreShelfEyebrow"));
  shelfLay->addWidget(shelfEyebrow_);
  shelfTitle_ =
      new QLabel(QStringLiteral("Discover Something New"), shelfHeader_);
  shelfTitle_->setObjectName(QStringLiteral("aioStoreShelfTitle"));
  shelfLay->addWidget(shelfTitle_);
  shelfSummary_ = new QLabel(
      QStringLiteral(
          "Your game catalog — powered by Steam and your local library."),
      shelfHeader_);
  shelfSummary_->setObjectName(QStringLiteral("aioStoreShelfSummary"));
  shelfSummary_->setWordWrap(true);
  shelfLay->addWidget(shelfSummary_);
  outer->addWidget(shelfHeader_);

  controlsBar_ = new QWidget(this);
  controlsBar_->setObjectName(QStringLiteral("aioStoreControlsBar"));
  auto *controlsLay = new QHBoxLayout(controlsBar_);
  controlsLay->setContentsMargins(48, 0, 48, 8);
  controlsLay->setSpacing(12);

  searchPanel_ = new QWidget(controlsBar_);
  searchPanel_->setObjectName(QStringLiteral("aioStoreSearchPanel"));
  auto *searchLay = new QHBoxLayout(searchPanel_);
  searchLay->setContentsMargins(18, 10, 18, 10);
  searchLay->setSpacing(12);
  searchLabel_ = new QLabel(QStringLiteral("Steam Search"), searchPanel_);
  searchLabel_->setObjectName(QStringLiteral("aioStoreSearchLabel"));
  searchLay->addWidget(searchLabel_);
  searchValue_ =
      new QLabel(QStringLiteral("Type to search titles"), searchPanel_);
  searchValue_->setObjectName(QStringLiteral("aioStoreSearchValue"));
  searchLay->addWidget(searchValue_);
  controlsLay->addWidget(searchPanel_, 1);

  libraryFilterBar_ = new QWidget(controlsBar_);
  libraryFilterBar_->setObjectName(QStringLiteral("aioGamesFilterBar"));
  auto *libraryFilterLay = new QHBoxLayout(libraryFilterBar_);
  libraryFilterLay->setContentsMargins(0, 0, 0, 0);
  libraryFilterLay->setSpacing(8);
  const QStringList libraryFilters = {
      QStringLiteral("All"), QStringLiteral("Steam"), QStringLiteral("Local"),
      QStringLiteral("Unsupported")};
  for (const QString &filterLabel : libraryFilters) {
    auto *label = new QLabel(filterLabel, libraryFilterBar_);
    label->setObjectName(QStringLiteral("aioGamesFilterChip"));
    label->setAlignment(Qt::AlignCenter);
    libraryFilterLay->addWidget(label);
    libraryFilterLabels_.append(label);
  }
  controlsLay->addWidget(libraryFilterBar_);
  outer->addWidget(controlsBar_);

  contentArea_ = new QWidget(this);
  auto *contLay = new QHBoxLayout(contentArea_);
  contLay->setContentsMargins(0, 0, 0, 0);
  contLay->setSpacing(0);
  gridScroll_ = new QScrollArea(this);
  gridScroll_->setWidgetResizable(true);
  gridScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  gridScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  gridScroll_->setFrameShape(QFrame::NoFrame);
  gridHost_ = new QWidget();
  gridHost_->setObjectName(QStringLiteral("aioStoreGridHost"));
  gridHost_->setAttribute(Qt::WA_StyledBackground, true);
  gridScroll_->setWidget(gridHost_);
  connect(gridScroll_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int) { maybeLoadMoreCatalog(); });
  contLay->addWidget(gridScroll_, 1);

  detailOverlay_ = new QWidget(this);
  detailOverlay_->setObjectName(QStringLiteral("aioStoreDetailOverlay"));
  detailOverlay_->setAttribute(Qt::WA_StyledBackground, true);
  detailOverlay_->hide();

  detailPanel_ = new QFrame(detailOverlay_);
  detailPanel_->setObjectName(QStringLiteral("aioStoreDetailPanel"));
  detailPanel_->setVisible(false);
  auto *detLay = new QVBoxLayout(detailPanel_);
  detLay->setContentsMargins(24, 24, 24, 24);
  detLay->setSpacing(12);
  detailArt_ = new QLabel(detailPanel_);
  detailArt_->setObjectName(QStringLiteral("aioStoreSkeletonThumb"));
  detailArt_->setFixedHeight(200);
  detailArt_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  detLay->addWidget(detailArt_);
  detailTitle_ = new QLabel(detailPanel_);
  detailTitle_->setObjectName(QStringLiteral("aioStoreDetailTitle"));
  detailTitle_->setWordWrap(true);
  detLay->addWidget(detailTitle_);
  auto *metaRow = new QHBoxLayout();
  detailPublisher_ = new QLabel(detailPanel_);
  detailPublisher_->setObjectName(QStringLiteral("aioStoreDetailMeta"));
  metaRow->addWidget(detailPublisher_);
  detailYear_ = new QLabel(detailPanel_);
  detailYear_->setObjectName(QStringLiteral("aioStoreDetailMeta"));
  metaRow->addWidget(detailYear_);
  metaRow->addStretch();
  detLay->addLayout(metaRow);
  detailRating_ = new QLabel(detailPanel_);
  detailRating_->setObjectName(QStringLiteral("aioStoreDetailRating"));
  detLay->addWidget(detailRating_);
  detailCategory_ = new QLabel(detailPanel_);
  detailCategory_->setObjectName(QStringLiteral("aioStoreDetailCategory"));
  detLay->addWidget(detailCategory_);
  detailDescription_ = new QLabel(detailPanel_);
  detailDescription_->setObjectName(QStringLiteral("aioStoreDetailDesc"));
  detailDescription_->setWordWrap(true);
  detLay->addWidget(detailDescription_);
  detLay->addSpacing(24);
  installBtn_ = new QPushButton(QStringLiteral("Play / Install"), detailPanel_);
  installBtn_->setObjectName(QStringLiteral("aioStorePlayButton"));
  installBtn_->setFocusPolicy(Qt::NoFocus);
  connect(installBtn_, &QPushButton::clicked, this, [this]() {
    const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
    if (idx >= 0 && idx < filteredGames_.size()) {
      const StoreGame &game = filteredGames_[idx];
      if (game.isRomGame)
        emit romLaunchRequested(game.id);
      else
        emit gameSelected(game.id);
    }
  });
  detLay->addWidget(installBtn_);
  outer->addWidget(contentArea_, 1);
  // Toast
  toastWidget_ = new QWidget(this);
  toastWidget_->setObjectName(QStringLiteral("aioStoreToast"));
  toastWidget_->setVisible(false);
  auto *toastLay = new QHBoxLayout(toastWidget_);
  toastLay->setContentsMargins(16, 8, 16, 8);
  auto *toastLbl = new QLabel(
      QStringLiteral("Coming Soon - Installation is not yet available"),
      toastWidget_);
  toastLbl->setAlignment(Qt::AlignCenter);
  toastLay->addWidget(toastLbl);
  toastTimer_ = new QTimer(this);
  toastTimer_->setSingleShot(true);
  connect(toastTimer_, &QTimer::timeout, toastWidget_, &QWidget::hide);

  buildAccountPage();
  outer->addWidget(accountPage_, 1);

  connect(&ThumbnailCache::instance(), &ThumbnailCache::thumbnailReady, this,
          [this](const QString &url) {
            if (!detailVisible_ || filteredGames_.isEmpty())
              return;
            const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
            if (idx < 0 || idx >= filteredGames_.size())
              return;
            const StoreGame &focusedGame = filteredGames_.at(idx);
            if (focusedGame.coverArtUrl == url ||
                focusedGame.coverArtUrls.contains(url))
              showDetailPanel(focusedGame);
          });

  rebuildGrid();
  resetGridViewport();
  updateTabFocus();
  updateGridFocus();
  updateShelfHeader();
}

void GameStorePage::resetGridViewport() {
  if (!gridScroll_)
    return;
  if (QScrollBar *bar = gridScroll_->verticalScrollBar())
    bar->setValue(0);
}

void GameStorePage::updateDetailOverlayLayout() {
  if (!detailOverlay_ || !detailPanel_)
    return;

  detailOverlay_->setGeometry(rect());
  const int panelWidth = qBound(380, width() * 2 / 5, 540);
  const int topInset = 64;
  const int bottomInset = 32;
  const int panelHeight = qMax(400, height() - topInset - bottomInset);
  detailPanel_->setGeometry(width() - panelWidth - 32, topInset, panelWidth,
                            panelHeight);
}

void GameStorePage::rebuildGrid() {
  kGridCols = computeGridCols();
  const int previousFocusIndex = gridFocusRow_ * kGridCols + gridFocusCol_;
  cards_.clear();
  if (!gridHost_)
    return;
  const auto childWidgets =
      gridHost_->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
  for (QWidget *child : childWidgets)
    delete child;
  delete gridHost_->layout();

  if (filteredGames_.isEmpty()) {
    auto *stateLay = new QVBoxLayout(gridHost_);
    stateLay->setContentsMargins(48, 24, 48, 48);
    stateLay->setSpacing(0);
    stateLay->addStretch();

    auto *stateCard = new QFrame(gridHost_);
    stateCard->setObjectName(QStringLiteral("aioStoreStateCard"));
    stateCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *cardLay = new QVBoxLayout(stateCard);
    cardLay->setContentsMargins(32, 32, 32, 32);
    cardLay->setSpacing(12);

    auto *stateTitle = new QLabel(stateCard);
    stateTitle->setObjectName(QStringLiteral("aioStoreStateTitle"));
    stateTitle->setAlignment(Qt::AlignCenter);
    auto *stateBody = new QLabel(stateCard);
    stateBody->setObjectName(QStringLiteral("aioStoreStateBody"));
    stateBody->setAlignment(Qt::AlignCenter);
    stateBody->setWordWrap(true);

    if (catalogLoading_) {
      stateTitle->setText(QStringLiteral("Loading catalog"));
      stateBody->setText(QStringLiteral(
          "Pulling in store listings, discounts, and your local library."));
    } else if (errorShown_) {
      stateCard->setObjectName(QStringLiteral("aioStoreErrorCard"));
      stateTitle->setText(QStringLiteral("Store temporarily unavailable"));
      stateBody->setText(
          errorMessage_.isEmpty()
              ? QStringLiteral("The catalog could not be loaded right now. Try "
                               "again shortly or browse My Library.")
              : errorMessage_);
    } else {
      stateTitle->setText(QStringLiteral("Nothing here yet"));
      if (libraryModeActive_) {
        stateBody->setText(QStringLiteral(
            "Add ROMs to your configured library folder or install games "
            "through the store to populate this shelf."));
      } else {
        stateBody->setText(
            QStringLiteral("This shelf is empty right now. Try another "
                           "category or open My Library."));
      }
    }

    cardLay->addWidget(stateTitle);
    cardLay->addWidget(stateBody);
    stateLay->addWidget(stateCard, 0, Qt::AlignCenter);
    stateLay->addStretch();
    gridFocusRow_ = 0;
    gridFocusCol_ = 0;
    updateShelfHeader();
    return;
  }

  auto *grid = new QGridLayout(gridHost_);
  grid->setContentsMargins(48, 16, 48, 48);
  grid->setHorizontalSpacing(20);
  grid->setVerticalSpacing(20);

  const int prefetchCount =
      std::min(static_cast<int>(filteredGames_.size()), kGridCols * 3);
  for (int i = 0; i < prefetchCount; ++i) {
    const QStringList coverUrls =
        filteredGames_[i].coverArtUrls.isEmpty()
            ? QStringList{filteredGames_[i].coverArtUrl}
            : filteredGames_[i].coverArtUrls;
    if (!coverUrls.isEmpty() && !coverUrls.first().isEmpty())
      ThumbnailCache::instance().request(coverUrls.first());
  }

  for (int i = 0; i < filteredGames_.size(); ++i) {
    auto *card = new GameCard(filteredGames_[i], gridHost_);
    grid->addWidget(card, i / kGridCols, i % kGridCols);
    cards_.append(card);
  }
  for (int c = 0; c < kGridCols; ++c)
    grid->setColumnStretch(c, 1);
  const int restoredIndex = qBound(0, previousFocusIndex, cards_.size() - 1);
  gridFocusRow_ = restoredIndex / kGridCols;
  gridFocusCol_ = restoredIndex % kGridCols;
  updateShelfHeader();
  updateGridFocus();
}

int GameStorePage::colsInGrid() const { return kGridCols; }
int GameStorePage::rowsInGrid() const {
  return cards_.isEmpty() ? 0 : (cards_.size() - 1) / kGridCols + 1;
}

void GameStorePage::updateTabFocus() {
  for (int i = 0; i < tabLabels_.size(); ++i) {
    const bool focused = (focusArea_ == FocusArea::Tabs && i == tabFocus_);
    const bool active = (i == activeCategoryIndex_);
    tabLabels_[i]->setProperty("aio_selected", focused ? "true" : "false");
    tabLabels_[i]->setProperty("aio_active", active ? "true" : "false");
    tabLabels_[i]->style()->unpolish(tabLabels_[i]);
    tabLabels_[i]->style()->polish(tabLabels_[i]);
  }
  updateLibraryFilterFocus();
}

void GameStorePage::updateGridFocus() {
  for (int i = 0; i < cards_.size(); ++i) {
    const int row = i / kGridCols;
    const int col = i % kGridCols;
    const bool sel = (focusArea_ == FocusArea::Grid && row == gridFocusRow_ &&
                      col == gridFocusCol_);
    cards_[i]->setSelected(sel);
  }
  if (!cards_.isEmpty() && focusArea_ == FocusArea::Grid) {
    const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
    if (idx >= 0 && idx < cards_.size())
      gridScroll_->ensureWidgetVisible(cards_[idx]);
  }
  maybeLoadMoreCatalog();
}

void GameStorePage::maybeLoadMoreCatalog() {
  if (libraryModeActive_ || !catalogHasMore_ || catalogRequestInFlight_ ||
      !gridScroll_) {
    return;
  }

  bool nearScrollEnd = false;
  if (QScrollBar *bar = gridScroll_->verticalScrollBar()) {
    nearScrollEnd = (bar->maximum() - bar->value()) <= 720;
  }

  const int focusedIndex = gridFocusRow_ * kGridCols + gridFocusCol_;
  const bool nearFocusEnd =
      focusedIndex >= 0 &&
      (filteredGames_.size() - focusedIndex) <= (kGridCols * 2 + 1);
  if (nearScrollEnd || nearFocusEnd)
    requestCatalogPage(false);
}

void GameStorePage::activateFocusedGame() {
  const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
  if (idx < 0 || idx >= filteredGames_.size())
    return;
  showDetailPanel(filteredGames_[idx]);
}

void GameStorePage::showDetailPanel(const StoreGame &game) {
  detailVisible_ = true;
  if (detailOverlay_) {
    detailOverlay_->show();
    detailOverlay_->raise();
  }
  updateDetailOverlayLayout();
  detailPanel_->setVisible(true);
  detailPanel_->raise();
  QPixmap cachedArt;
  bool hasArt = false;
  const QStringList coverUrls = game.coverArtUrls.isEmpty()
                                    ? QStringList{game.coverArtUrl}
                                    : game.coverArtUrls;
  for (const QString &coverUrl : coverUrls) {
    if (!coverUrl.isEmpty() &&
        ThumbnailCache::instance().tryGet(coverUrl, &cachedArt)) {
      hasArt = true;
      break;
    }
  }
  if (!hasArt) {
    for (const QString &coverUrl : coverUrls) {
      if (!coverUrl.isEmpty()) {
        ThumbnailCache::instance().request(coverUrl);
        break;
      }
    }
  }
  if (hasArt) {
    QPixmap artPm(detailPanel_->width() - 48, 200);
    artPm.fill(Qt::transparent);
    QPainter p(&artPm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath pp;
    pp.addRoundedRect(QRectF(artPm.rect()), 12, 12);

    const QPixmap scaled = cachedArt.scaled(
        artPm.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect target = artPm.rect();
    const QRect source((scaled.width() - target.width()) / 2,
                       (scaled.height() - target.height()) / 2, target.width(),
                       target.height());
    p.save();
    p.setClipPath(pp);
    p.drawPixmap(target, scaled, source);
    p.restore();

    QLinearGradient overlay(0, 0, 0, artPm.height());
    overlay.setColorAt(0.0, QColor(0, 0, 0, 12));
    overlay.setColorAt(0.55, QColor(0, 0, 0, 0));
    overlay.setColorAt(1.0, QColor(0, 0, 0, 180));
    p.fillPath(pp, overlay);

    p.setPen(QColor(255, 255, 255, 200));
    QFont f;
    f.setPixelSize(18);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    const QRectF textRect(16.0, artPm.height() - 48.0, artPm.width() - 32.0,
                          32.0);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, game.title);

    detailArt_->setObjectName(QStringLiteral("aioStoreDetailArt"));
    detailArt_->setPixmap(artPm);
  } else {
    detailArt_->setPixmap(QPixmap());
    detailArt_->setObjectName(QStringLiteral("aioStoreSkeletonThumb"));
  }
  detailArt_->style()->unpolish(detailArt_);
  detailArt_->style()->polish(detailArt_);
  detailTitle_->setText(game.title);

  if (game.isRomGame) {
    // ROM library game — show console type, no price
    detailPublisher_->setText(game.category);
    detailYear_->setText(QStringLiteral("Local library launch"));
    detailRating_->setText(QStringLiteral("Ready to run inside AIO Server"));
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        QStringLiteral("This ROM is in your local games library. "
                       "Press Enter or click below to launch it."));
    installBtn_->setText(QStringLiteral("Launch Game"));
    installBtn_->setObjectName(QStringLiteral("aioStorePlayButton"));
  } else if (game.hideCommerce) {
    detailPublisher_->setText(
        game.isInstalled ? QStringLiteral("Installed in your Steam library")
                         : libraryStatusText(game));
    detailYear_->setText(
        game.isInstalled
            ? QStringLiteral("Launch routed through the Steam client")
            : QStringLiteral(
                  "Open the embedded Steam store to manage ownership"));
    detailRating_->setText(
        game.isInstalled
            ? QStringLiteral("Play directly from AIO Server")
            : QStringLiteral(
                  "Sign in with your Steam account to install or purchase"));
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        game.isInstalled
            ? QStringLiteral(
                  "This title is already on your device or linked "
                  "app library. Launch it here and keep the store, DLC, "
                  "and community pages available in the shell.")
            : QStringLiteral(
                  "This title is available from the store catalog. "
                  "Open the embedded Steam page to sign in with your actual "
                  "account, complete checkout, and trigger install without "
                  "leaving AIO Server."));
    installBtn_->setText(game.isInstalled ? QStringLiteral("Launch via Steam")
                                          : QStringLiteral("Open in Store"));
    installBtn_->setObjectName(game.isInstalled
                                   ? QStringLiteral("aioStorePlayButton")
                                   : QStringLiteral("aioStoreInstallButton"));
  } else if (game.isOnSale && game.discountPercent > 0 &&
             game.priceUsdCents > 0) {
    const int origCents = static_cast<int>(game.priceUsdCents * 100.0 /
                                           (100 - game.discountPercent));
    detailPublisher_->setText(formatPrice(game.priceUsdCents));
    detailYear_->setText(
        QStringLiteral("(was %1)").arg(formatPrice(origCents)));
    detailRating_->setText(
        QStringLiteral("SAVE %1%  then install or launch through Steam")
            .arg(game.discountPercent));
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        game.isInstalled
            ? QStringLiteral(
                  "This game is in your library. "
                  "Launch it from AIO Server and keep Steam pages in-app for "
                  "updates, DLC, and account tasks.")
            : QStringLiteral(
                  "Open the catalog to browse screenshots, "
                  "read reviews, sign in with your actual Steam account, "
                  "purchase, and start install."));
    installBtn_->setText(game.isInstalled ? QStringLiteral("Launch via Steam")
                                          : QStringLiteral("Open in Store"));
    installBtn_->setObjectName(game.isInstalled
                                   ? QStringLiteral("aioStorePlayButton")
                                   : QStringLiteral("aioStoreInstallButton"));
  } else {
    detailPublisher_->setText(formatPrice(game.priceUsdCents));
    detailYear_->setText(
        game.isInstalled
            ? QStringLiteral("Installed in your Steam library")
            : (game.priceUsdCents > 0
                   ? QStringLiteral("Purchase and install available in-app")
                   : QStringLiteral("Free to install via Steam")));
    detailRating_->setText(
        game.isInstalled ? QStringLiteral("Launch from AIO Server")
                         : QStringLiteral("Steam account sign-in supported"));
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        game.isInstalled
            ? QStringLiteral(
                  "This game is in your library. "
                  "Press Enter or click below to launch it through the Steam "
                  "client while staying in the shell.")
            : QStringLiteral(
                  "Open the catalog to browse screenshots, "
                  "read reviews, sign in with your actual Steam account, "
                  "purchase, and trigger install."));
    installBtn_->setText(game.isInstalled ? QStringLiteral("Launch via Steam")
                                          : QStringLiteral("Open in Store"));
    installBtn_->setObjectName(game.isInstalled
                                   ? QStringLiteral("aioStorePlayButton")
                                   : QStringLiteral("aioStoreInstallButton"));
  }
  installBtn_->style()->unpolish(installBtn_);
  installBtn_->style()->polish(installBtn_);
  detailCategory_->setText(game.isRomGame ? QStringLiteral("Local Library")
                                          : game.category);
  detailCategory_->setVisible(!detailCategory_->text().isEmpty() &&
                              detailCategory_->text() != QStringLiteral("All"));
}

void GameStorePage::clearDetailPanel() {
  detailVisible_ = false;
  detailPanel_->setVisible(false);
  if (detailOverlay_)
    detailOverlay_->hide();
  focusArea_ = FocusArea::Grid;
  updateGridFocus();
  updateTabFocus();
}

// ── Steam account / sign-in overlay ──────────────────────────────────────────

void GameStorePage::updateAccountStatus() {
  if (!accountStatusLabel_)
    return;
  if (steamService_ && steamService_->hasSteamId()) {
    accountStatusLabel_->setText(
        QStringLiteral("Steam ID: %1")
            .arg(steamService_->steamId64().left(8) + QStringLiteral("…")));
    accountStatusLabel_->setProperty("aio_auth_state", "signed-in");
  } else {
    accountStatusLabel_->setText(QStringLiteral("Sign in to Steam  ↵"));
    accountStatusLabel_->setProperty("aio_auth_state", "signed-out");
  }
  accountStatusLabel_->style()->unpolish(accountStatusLabel_);
  accountStatusLabel_->style()->polish(accountStatusLabel_);
}

void GameStorePage::buildAccountPage() {
  accountPage_ = new QWidget(this);
  accountPage_->setObjectName(QStringLiteral("aioStoreAccountPage"));
  accountPage_->setAttribute(Qt::WA_StyledBackground, true);
  accountPage_->hide();

  auto *pageLay = new QVBoxLayout(accountPage_);
  pageLay->setContentsMargins(48, 0, 48, 48);
  pageLay->setSpacing(0);
  pageLay->addStretch(1);

  auto *cardRow = new QHBoxLayout();
  auto *formCard = new QFrame(accountPage_);
  formCard->setObjectName(QStringLiteral("aioStoreAccountCard"));
  formCard->setFixedWidth(600);

  auto *lay = new QVBoxLayout(formCard);
  lay->setContentsMargins(40, 36, 40, 36);
  lay->setSpacing(0);

  auto *titleLbl = new QLabel(QStringLiteral("Steam Account"), formCard);
  titleLbl->setObjectName(QStringLiteral("aioStoreAccountCardTitle"));
  lay->addWidget(titleLbl);
  lay->addSpacing(12);

  auto *infoLbl = new QLabel(
      QStringLiteral("Sign in with your Steam account to load your full owned "
                     "library.\n\n"
                     "Your Game Details privacy setting must be Public in "
                     "Steam \xC2\xBB Privacy Settings."),
      formCard);
  infoLbl->setObjectName(QStringLiteral("aioStoreAccountCardInfo"));
  infoLbl->setWordWrap(true);
  lay->addWidget(infoLbl);
  lay->addSpacing(28);

  auto *div = new QFrame(formCard);
  div->setObjectName(QStringLiteral("aioStoreAccountDivider"));
  div->setFrameShape(QFrame::HLine);
  lay->addWidget(div);
  lay->addSpacing(24);

  // Status label (signed-in state / errors)
  signinStatusLabel_ = new QLabel(formCard);
  signinStatusLabel_->setObjectName(QStringLiteral("aioStoreAccountMsg"));
  signinStatusLabel_->setWordWrap(true);
  signinStatusLabel_->setAlignment(Qt::AlignCenter);
  signinStatusLabel_->hide();
  lay->addWidget(signinStatusLabel_);
  lay->addSpacing(4);

  // Primary CTA — full-width prominent button
  auto *signInBtn =
      new QPushButton(QStringLiteral("Sign in with Steam"), formCard);
  signInBtn->setObjectName(QStringLiteral("aioStoreAccountBtn"));
  signInBtn->setProperty("aio_role", QStringLiteral("primary"));
  signInBtn->setProperty("aio_focused", QStringLiteral("true"));
  signInBtn->setFocusPolicy(Qt::NoFocus);
  signInBtn->setCursor(Qt::PointingHandCursor);
  connect(signInBtn, &QPushButton::clicked, this,
          &GameStorePage::openSteamAuthDialog);
  lay->addWidget(signInBtn);
  lay->addSpacing(10);

  // Secondary — Disconnect
  auto *disconnectBtn = new QPushButton(QStringLiteral("Disconnect"), formCard);
  disconnectBtn->setObjectName(QStringLiteral("aioStoreAccountBtn"));
  disconnectBtn->setProperty("aio_role", QStringLiteral("secondary"));
  disconnectBtn->setFocusPolicy(Qt::NoFocus);
  disconnectBtn->setCursor(Qt::PointingHandCursor);
  connect(disconnectBtn, &QPushButton::clicked, this, [this]() {
    if (steamService_) {
      steamService_->clearApiCredentials();
      updateAccountStatus();
      if (signinStatusLabel_) {
        signinStatusLabel_->setText(QStringLiteral("Disconnected from Steam."));
        signinStatusLabel_->setProperty("aio_tone", QStringLiteral(""));
        signinStatusLabel_->show();
      }
    }
  });
  lay->addWidget(disconnectBtn);
  lay->addSpacing(20);

  auto *hintLbl = new QLabel(
      QStringLiteral(
          "\u2191\u2193 Navigate   \u00b7   Enter   \u00b7   Esc back"),
      formCard);
  hintLbl->setObjectName(QStringLiteral("aioStoreAccountHint"));
  hintLbl->setAlignment(Qt::AlignCenter);
  lay->addWidget(hintLbl);

  cardRow->addStretch(1);
  cardRow->addWidget(formCard);
  cardRow->addStretch(1);
  pageLay->addLayout(cardRow);
  pageLay->addStretch(1);
}

void GameStorePage::hideAccountPage() {
  if (accountPage_)
    accountPage_->hide();
  if (shelfHeader_)
    shelfHeader_->show();
  if (controlsBar_)
    controlsBar_->show();
  if (contentArea_)
    contentArea_->show();
  activeCategoryIndex_ = 0;
  tabFocus_ = 0;
  focusArea_ = FocusArea::Tabs;
  updateTabFocus();
}

void GameStorePage::openSteamAuthDialog() {
  // Resolve port the same way SteamService does
  const auto getPort = []() -> int {
    const QStringList candidates = {
        QDir(QDir::currentPath()).filePath(QStringLiteral("server")),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../server")),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../../server")),
    };
    for (const QString &dir : candidates) {
      const QString envPath = QDir(dir).filePath(QStringLiteral(".env"));
      QFile f(envPath);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        continue;
      QTextStream ts(&f);
      while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.startsWith(QStringLiteral("PORT="))) {
          bool ok = false;
          const int p = line.mid(5).trimmed().toInt(&ok);
          if (ok && p > 0)
            return p;
        }
      }
    }
    return 8916;
  };

  auto *dlg = new SteamAuthDialog(getPort(), window());
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  connect(dlg, &SteamAuthDialog::authComplete, this,
          [this](const QString &steamId) {
            if (!steamService_)
              return;
            steamService_->setSteamId(steamId);
            updateAccountStatus();

            if (signinStatusLabel_) {
              signinStatusLabel_->setText(QStringLiteral(
                  "Signed in \xE2\x80\x94 loading library\xE2\x80\xa6"));
              signinStatusLabel_->setProperty("aio_tone", "");
              signinStatusLabel_->show();
            }

            // Reset so the success path in onOwnedLibraryReady marks it done
            ownedLibraryFetched_ = false;
            steamService_->fetchOwnedLibraryXml();
          });

  dlg->exec();
}

void GameStorePage::handleSignInKey(QKeyEvent *event) {
  const int key = event->key();

  if (key == Qt::Key_Escape) {
    hideAccountPage();
    event->accept();
    return;
  }

  if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up ||
      key == Qt::Key_Down) {
    signinFocus_ = (signinFocus_ == 0) ? 1 : 0;
    // Update button focus visuals
    if (accountPage_) {
      const auto btns = accountPage_->findChildren<QPushButton *>(
          QStringLiteral("aioStoreAccountBtn"));
      for (int i = 0; i < btns.size(); ++i) {
        btns[i]->setProperty("aio_focused",
                             (i == signinFocus_) ? "true" : "false");
        btns[i]->style()->unpolish(btns[i]);
        btns[i]->style()->polish(btns[i]);
      }
    }
    event->accept();
    return;
  }

  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (signinFocus_ == 0) {
      openSteamAuthDialog();
    } else {
      // Disconnect button
      if (steamService_) {
        steamService_->clearApiCredentials();
        updateAccountStatus();
        if (signinStatusLabel_) {
          signinStatusLabel_->setText(
              QStringLiteral("Disconnected from Steam."));
          signinStatusLabel_->setProperty("aio_tone", "");
          signinStatusLabel_->show();
        }
      }
    }
    event->accept();
    return;
  }

  event->ignore();
}

void GameStorePage::onOwnedLibraryReady(
    const QSet<int> &ownedAppIds, const QList<AIO::GUI::SteamGame> &games) {
  ownedAppIds_ = ownedAppIds;
  ownedSteamGames_ = games;

  // Mark owned games in the active catalog
  for (StoreGame &g : allGames_) {
    if (!g.isRomGame) {
      g.isOwned = ownedAppIds.contains(g.id.toInt());
    }
  }

  updateAccountStatus();

  if (signinStatusLabel_)
    signinStatusLabel_->setVisible(false);

  ownedLibraryFetched_ = true; // mark success so auto-fetch doesn't loop

  // Re-scan library to include newly-owned games
  scanLibrary();
  applyActiveCategoryFilter();
  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
}

void GameStorePage::onSteamAuthError(const QString &message) {
  if (focusArea_ == FocusArea::SignIn && signinStatusLabel_) {
    signinStatusLabel_->setText(message);
    signinStatusLabel_->setVisible(true);
  } else {
    // Show as a transient error in the shelf summary if the store is visible
    errorShown_ = true;
    errorMessage_ = QStringLiteral("Steam account: %1").arg(message);
    updateShelfHeader();
  }
}

void GameStorePage::showComingSoonToast() {
  if (!toastWidget_)
    return;
  toastWidget_->setGeometry((width() - 420) / 2, height() - 80, 420, 52);
  toastWidget_->raise();
  toastWidget_->show();
  toastTimer_->start(2500);
}

void GameStorePage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  requestCatalogIfNeeded();
  focusArea_ = FocusArea::Grid;
  detailVisible_ = false;
  if (detailOverlay_)
    detailOverlay_->hide();
  if (detailPanel_)
    detailPanel_->hide();
  gridFocusRow_ = 0;
  gridFocusCol_ = 0;
  scanLibrary();
  applyActiveCategoryFilter();
  const int newCols = computeGridCols();
  if (newCols != kGridCols)
    rebuildGrid();
  else
    rebuildGrid();
  resetGridViewport();
  updateTabFocus();
  updateGridFocus();
  updateSearchControls();
  setFocus();
}
void GameStorePage::keyPressEvent(QKeyEvent *event) {
  // Sign-in overlay has exclusive key focus
  if (focusArea_ == FocusArea::SignIn) {
    handleSignInKey(event);
    return;
  }

  if (handleSearchKey(event))
    return;

  const int key = event->key();
  switch (focusArea_) {
  case FocusArea::Tabs:
    if (key == Qt::Key_Left) {
      tabFocus_ = qMax(0, tabFocus_ - 1);
      updateTabFocus();
    } else if (key == Qt::Key_Right) {
      tabFocus_ = qMin(categories_.size() - 1, tabFocus_ + 1);
      updateTabFocus();
    } else if (key == Qt::Key_Down) {
      if (libraryModeActive_ && !libraryFilterLabels_.isEmpty()) {
        focusArea_ = FocusArea::LibraryFilters;
        libraryFilterFocus_ = static_cast<int>(libraryFilter_);
        updateLibraryFilterFocus();
      } else {
        focusArea_ = FocusArea::Grid;
      }
      updateTabFocus();
      updateGridFocus();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      activeCategoryIndex_ = tabFocus_;
      libraryFilter_ = LibraryFilter::All;
      libraryFilterFocus_ = 0;
      applyActiveCategoryFilter();
      // Account tab: applyActiveCategoryFilter shows the account page
      if (activeCategoryIndex_ < categories_.size() &&
          categories_.at(activeCategoryIndex_) == QStringLiteral("Account")) {
        updateTabFocus();
        break;
      }
      if (categories_.at(activeCategoryIndex_) ==
          QStringLiteral("My Library")) {
        rebuildGrid();
        resetGridViewport();
        // If the library is empty but we have a Steam ID, kick off a fresh
        // fetch — covers the case where the server wasn't up at startup.
        if (libraryGames_.isEmpty() && steamService_ &&
            steamService_->hasSteamId()) {
          steamService_->fetchOwnedLibraryXml();
        }
      } else {
        requestCatalogPage(true);
      }
      focusArea_ = FocusArea::Grid;
      updateTabFocus();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
      emit homeRequested();
    } else {
      QWidget::keyPressEvent(event);
    }
    break;
  case FocusArea::LibraryFilters:
    if (key == Qt::Key_Left) {
      libraryFilterFocus_ = qMax(0, libraryFilterFocus_ - 1);
      updateLibraryFilterFocus();
    } else if (key == Qt::Key_Right) {
      libraryFilterFocus_ =
          qMin(libraryFilterLabels_.size() - 1, libraryFilterFocus_ + 1);
      updateLibraryFilterFocus();
    } else if (key == Qt::Key_Up) {
      focusArea_ = FocusArea::Tabs;
      updateTabFocus();
      updateGridFocus();
    } else if (key == Qt::Key_Down) {
      focusArea_ = FocusArea::Grid;
      updateTabFocus();
      updateGridFocus();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      libraryFilter_ = static_cast<LibraryFilter>(libraryFilterFocus_);
      applyActiveCategoryFilter();
      rebuildGrid();
      resetGridViewport();
      focusArea_ = FocusArea::Grid;
      updateTabFocus();
      updateGridFocus();
    } else if (key == Qt::Key_Escape) {
      emit homeRequested();
    } else {
      QWidget::keyPressEvent(event);
    }
    break;
  case FocusArea::Grid:
    if (key == Qt::Key_Up) {
      if (gridFocusRow_ > 0) {
        --gridFocusRow_;
        updateGridFocus();
      } else {
        if (libraryModeActive_ && !libraryFilterLabels_.isEmpty()) {
          focusArea_ = FocusArea::LibraryFilters;
          libraryFilterFocus_ = static_cast<int>(libraryFilter_);
          updateLibraryFilterFocus();
        } else {
          focusArea_ = FocusArea::Tabs;
          tabFocus_ = activeCategoryIndex_;
        }
        updateTabFocus();
        updateGridFocus();
      }
    } else if (key == Qt::Key_Down) {
      if (gridFocusRow_ + 1 < rowsInGrid()) {
        ++gridFocusRow_;
        const int maxCol = (gridFocusRow_ == rowsInGrid() - 1)
                               ? ((cards_.size() - 1) % kGridCols)
                               : (kGridCols - 1);
        gridFocusCol_ = qMin(gridFocusCol_, maxCol);
        updateGridFocus();
      }
    } else if (key == Qt::Key_Left) {
      if (gridFocusCol_ > 0) {
        --gridFocusCol_;
        updateGridFocus();
      }
    } else if (key == Qt::Key_Right) {
      const int maxCol = (gridFocusRow_ == rowsInGrid() - 1)
                             ? ((cards_.size() - 1) % kGridCols)
                             : (kGridCols - 1);
      if (gridFocusCol_ < maxCol) {
        ++gridFocusCol_;
        updateGridFocus();
      }
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      activateFocusedGame();
      focusArea_ = FocusArea::Detail;
      updateTabFocus();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
      emit homeRequested();
    } else {
      QWidget::keyPressEvent(event);
    }
    break;
  case FocusArea::Detail:
    if (key == Qt::Key_Down) {
      installBtn_->setFocus();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      // Launch / install via Steam or local ROM
      const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
      if (idx >= 0 && idx < filteredGames_.size()) {
        const StoreGame &game = filteredGames_[idx];
        if (game.isRomGame)
          emit romLaunchRequested(game.id);
        else
          emit gameSelected(game.id);
      }
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace ||
               key == Qt::Key_Left) {
      clearDetailPanel();
    } else {
      QWidget::keyPressEvent(event);
    }
    break;
  case FocusArea::SignIn:
    // Handled at the top of keyPressEvent before the switch
    break;
  }
}

} // namespace AIO::GUI
