#include "gui/GameStorePage.h"

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
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace AIO::GUI {

static QString formatPrice(int cents) {
  if (cents <= 0)
    return QStringLiteral("Free to Play");
  const int dollars = cents / 100;
  const int rem = cents % 100;
  return QStringLiteral("$%1.%2").arg(dollars).arg(rem, 2, 10, QChar('0'));
}

// GameCard: private to this translation unit
class GameCard : public QFrame {
public:
  explicit GameCard(const StoreGame &game, QWidget *parent = nullptr)
      : QFrame(parent), game_(game) {
    setObjectName(QStringLiteral("aioGameCard"));
    setFixedHeight(320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    titleLabel_ = new QLabel(game.title, this);
    titleLabel_->setObjectName(QStringLiteral("aioStoreCardTitle"));
    titleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont tf;
    tf.setPixelSize(16);
    tf.setWeight(QFont::Bold);
    titleLabel_->setFont(tf);
    const QString priceStr =
        game.isRomGame
            ? game.category
            : ((game.isOnSale && game.discountPercent > 0)
                   ? QStringLiteral("-%1% off").arg(game.discountPercent)
                   : formatPrice(game.priceUsdCents));
    pubLabel_ = new QLabel(priceStr, this);
    pubLabel_->setObjectName(QStringLiteral("aioStoreCardMeta"));
    pubLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QFont pf;
    pf.setPixelSize(13);
    pubLabel_->setFont(pf);
    pubLabel_->setProperty("aio_tone",
                           game.isRomGame
                               ? QStringLiteral("library")
                               : ((game.isOnSale && game.discountPercent > 0)
                                      ? QStringLiteral("sale")
                                      : QStringLiteral("default")));
    lay->addStretch(1);
    lay->addWidget(titleLabel_);
    lay->addWidget(pubLabel_);
  }

  void setSelected(bool sel) {
    setProperty("aio_selected", sel);
    style()->unpolish(this);
    style()->polish(this);
    update();
  }

  const StoreGame &game() const { return game_; }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());
    const QRectF r(rect());
    const qreal radius = 16.0;
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0.0, game_.coverColor.lighter(140));
    grad.setColorAt(0.6, game_.coverColor);
    grad.setColorAt(1.0, game_.coverColor.darker(160));
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, grad);

    // Add structured geometry so cards stay visually distinct even
    // without real key art assets.
    {
      QPainterPath diagonal;
      diagonal.moveTo(r.left() - 16.0, r.top() + r.height() * 0.64);
      diagonal.lineTo(r.right(), r.top() + r.height() * 0.26);
      diagonal.lineTo(r.right(), r.top() + r.height() * 0.42);
      diagonal.lineTo(r.left() - 16.0, r.top() + r.height() * 0.80);
      diagonal.closeSubpath();
      p.fillPath(diagonal, QColor(255, 255, 255, 24));
    }

    {
      QFont initialFont;
      initialFont.setPixelSize(static_cast<int>(r.height() * 0.26));
      initialFont.setWeight(QFont::Bold);
      p.setFont(initialFont);
      p.setPen(QColor(255, 255, 255, 54));
      const QString initial = game_.title.isEmpty()
                                  ? QStringLiteral("?")
                                  : game_.title.left(1).toUpper();
      p.drawText(QRectF(r.left(), r.top() + 18, r.width(), r.height() * 0.55),
                 Qt::AlignCenter, initial);
    }

    QLinearGradient shine(r.topLeft(),
                          QPointF(r.left(), r.top() + r.height() * 0.3));
    shine.setColorAt(0.0, QColor(255, 255, 255, 28));
    shine.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(path, shine);

    // Sale badge (top-right corner) — only shown when game is discounted
    if (game_.isOnSale && game_.discountPercent > 0) {
      const QString saleText =
          QStringLiteral("-%1%").arg(game_.discountPercent);
      QFont badgeFont;
      badgeFont.setPixelSize(12);
      badgeFont.setWeight(QFont::Bold);
      p.setFont(badgeFont);
      const QFontMetrics bfm(badgeFont);
      const int bw = bfm.horizontalAdvance(saleText) + 14;
      const QRectF badgeRect(r.right() - bw - 6, r.top() + 8, bw, 22);
      QPainterPath badgePath;
      badgePath.addRoundedRect(badgeRect, 6, 6);
      p.fillPath(badgePath, QColor(46, 204, 113, 230));
      p.setPen(QColor(0, 0, 0, 220));
      p.drawText(badgeRect, Qt::AlignCenter, saleText);
    }

    // Installed badge — green chip in bottom-left
    if (game_.isInstalled) {
      const QString installedText = QStringLiteral("Installed");
      QFont instFont;
      instFont.setPixelSize(13);
      instFont.setWeight(QFont::Medium);
      p.setFont(instFont);
      const QFontMetrics fm(instFont);
      const int tw = fm.horizontalAdvance(installedText);
      const QRectF instRect(r.left() + 12, r.bottom() - 32, tw + 16, 24);
      QPainterPath instPath;
      instPath.addRoundedRect(instRect, 6, 6);
      p.fillPath(instPath, QColor(63, 185, 80, 200));
      p.setPen(QColor(0, 0, 0, 220));
      p.drawText(instRect, Qt::AlignCenter, installedText);
    }

    // Focus ring — crisp 3px white border at card boundary (tvOS/PS5 style)
    if (property("aio_selected").toBool()) {
      QPainterPath glow;
      glow.addRoundedRect(r.adjusted(3.0, 3.0, -3.0, -3.0), radius - 3.0,
                          radius - 3.0);
      p.setPen(QPen(QColor(255, 255, 255, 60), 8.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(glow);

      QPainterPath ring;
      ring.addRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1.5,
                          radius - 1.5);
      p.setPen(QPen(QColor(255, 255, 255, 236), 4.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(ring);
    }

    // Source chip — small badge top-left showing origin (Steam or console)
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
      const QRectF srcRect(r.left() + 12, r.top() + 12, sw, 22);
      QPainterPath srcPath;
      srcPath.addRoundedRect(srcRect, 6, 6);
      const QColor bg = game_.isRomGame ? QColor(156, 140, 255, 55)
                                        : QColor(100, 181, 246, 45);
      const QColor fg = game_.isRomGame ? QColor(200, 190, 255, 200)
                                        : QColor(150, 210, 255, 190);
      p.fillPath(srcPath, bg);
      p.setPen(fg);
      p.drawText(srcRect, Qt::AlignCenter, srcText);
    }
  }

private:
  StoreGame game_;
  QLabel *titleLabel_ = nullptr;
  QLabel *pubLabel_ = nullptr;
};

// GameStorePage
GameStorePage::GameStorePage(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("aioGameStorePage"));
  setFocusPolicy(Qt::StrongFocus);
  categories_.append(QStringLiteral("All"));
  categories_.append(QStringLiteral("My Library"));
  setupUi();
}

void GameStorePage::setSteamService(SteamService *service) {
  steamService_ = service;
  connect(steamService_, &SteamService::gamesReady, this,
          &GameStorePage::onSteamGamesReady);
  connect(steamService_, &SteamService::fetchError, this,
          &GameStorePage::showSteamError);
  requestCatalogIfNeeded();
}

void GameStorePage::requestCatalogIfNeeded() {
  if (!steamService_ || catalogRequested_)
    return;
  catalogRequested_ = true;
  catalogLoading_ = true;
  errorShown_ = false;
  errorMessage_.clear();
  rebuildGrid();
  steamService_->fetchTopGames();
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

  if (cat == QStringLiteral("My Library")) {
    filteredGames_ = libraryGames_;
    libraryModeActive_ = true;
  } else if (cat == QStringLiteral("All")) {
    filteredGames_ = allGames_;
    if (filteredGames_.isEmpty() && !libraryGames_.isEmpty())
      filteredGames_ = libraryGames_;
    libraryModeActive_ = false;
  } else {
    for (const auto &game : allGames_) {
      if (game.category == cat)
        filteredGames_.append(game);
    }
    libraryModeActive_ = false;
  }

  updateShelfHeader();
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
  shelfTitle_->setText(showingLibrary
                           ? QStringLiteral("Installed and Local Games")
                           : (activeCategory == QStringLiteral("All")
                                  ? QStringLiteral("Featured Catalog")
                                  : activeCategory));

  if (catalogLoading_ && allGames_.isEmpty()) {
    shelfSummary_->setText(
        QStringLiteral("Fetching curated catalog and live deals..."));
  } else if (errorShown_ && filteredGames_.isEmpty()) {
    shelfSummary_->setText(
        errorMessage_.isEmpty()
            ? QStringLiteral("The store is unavailable right now.")
            : errorMessage_);
  } else {
    const int totalCount = filteredGames_.size();
    if (showingLibrary) {
      shelfSummary_->setText(
          QStringLiteral("%1 ready to launch from your device and linked apps.")
              .arg(totalCount));
    } else if (activeCategory == QStringLiteral("All")) {
      shelfSummary_->setText(
          QStringLiteral("Browse %1 curated titles in a console-style catalog.")
              .arg(totalCount));
    } else {
      shelfSummary_->setText(QStringLiteral("%1 titles in %2.")
                                 .arg(totalCount)
                                 .arg(activeCategory));
    }
  }
}

void GameStorePage::onSteamGamesReady(const QList<AIO::GUI::SteamGame> &games) {
  catalogLoading_ = false;
  errorShown_ = false;
  errorMessage_.clear();

  steamGames_ = games;

  // Rebuild category list from incoming data
  QSet<QString> catSet;
  for (const SteamGame &g : games)
    catSet.insert(g.category);
  categories_.clear();
  categories_.append(QStringLiteral("All"));
  QStringList sorted(catSet.begin(), catSet.end());
  sorted.sort(Qt::CaseInsensitive);
  categories_.append(sorted);
  categories_.append(QStringLiteral("My Library"));

  // Rebuild tab labels
  for (auto *lbl : tabLabels_)
    delete lbl;
  tabLabels_.clear();
  if (tabBar_) {
    auto *tabLay = qobject_cast<QHBoxLayout *>(tabBar_->layout());
    if (tabLay) {
      // Remove all items except the trailing stretch
      while (tabLay->count() > 1)
        delete tabLay->takeAt(0);
      for (const QString &cat : categories_) {
        auto *lbl = new QLabel(cat, tabBar_);
        lbl->setObjectName(QStringLiteral("aioStoreTabLabel"));
        lbl->setAlignment(Qt::AlignCenter);
        tabLay->insertWidget(tabLay->count() - 1, lbl);
        tabLabels_.append(lbl);
      }
    }
  }

  // Convert to StoreGame for display
  allGames_.clear();
  const QList<QColor> coverPalette = {
      QColor(100, 181, 246), QColor(156, 140, 255), QColor(255, 107, 107),
      QColor(63, 185, 80),   QColor(251, 188, 4),   QColor(255, 138, 76),
      QColor(38, 166, 154),  QColor(171, 71, 188)};
  for (int i = 0; i < games.size(); ++i) {
    const SteamGame &sg = games.at(i);
    StoreGame g;
    g.id = QString::number(sg.appId);
    g.title = sg.name;
    g.category = sg.category;
    g.coverColor = coverPalette[i % coverPalette.size()];
    g.isInstalled = sg.isInstalled;
    g.priceUsdCents = sg.priceUsdCents;
    g.discountPercent = sg.discountPercent;
    g.isOnSale = sg.isOnSale;
    g.sourceLabel = QStringLiteral("STORE");
    allGames_.append(g);
  }

  activeCategoryIndex_ = 0;
  tabFocus_ = 0;
  scanLibrary();
  applyActiveCategoryFilter();
  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
}

void GameStorePage::showSteamError(const QString &msg) {
  catalogLoading_ = false;
  errorShown_ = true;
  errorMessage_ = msg;
  scanLibrary();
  applyActiveCategoryFilter();
  rebuildGrid();
}

void GameStorePage::loadCatalog() {
  // Static catalog loading removed. Data now comes from SteamService.
  // This method is retained for API compatibility.
}

int GameStorePage::computeGridCols() const {
  const int w = this->width();
  if (w >= 1440)
    return 5;
  if (w >= 1000)
    return 4;
  return 3;
}

void GameStorePage::scanLibrary() {
  libraryGames_.clear();

  // ROM games from configured ROM directory
  QSettings settings("AIOServer", "GBAEmulator");
  const QString romDir =
      settings.value("romDirectory", QDir::homePath()).toString();
  const QDir dir(romDir);
  if (dir.exists()) {
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
      libraryGames_.append(g);
      ++idx;
    }
    std::sort(libraryGames_.begin(), libraryGames_.end(),
              [](const StoreGame &a, const StoreGame &b) {
                return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
              });
  }

  // Installed Steam games (already in allGames_ list)
  for (const StoreGame &sg : allGames_) {
    if (sg.isInstalled)
      libraryGames_.append(sg);
  }
}

void GameStorePage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  const int newCols = computeGridCols();
  if (newCols != kGridCols)
    rebuildGrid();
}

void GameStorePage::setupUi() {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  headerBar_ = new QWidget(this);
  headerBar_->setObjectName(QStringLiteral("aioStoreHeader"));
  auto *hdrLay = new QHBoxLayout(headerBar_);
  hdrLay->setContentsMargins(48, 16, 48, 16);
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
  auto *storeSub = new QLabel(
      QStringLiteral(
          "Browse & discover games  ·  Your catalog & local library"),
      headerBar_);
  storeSub->setObjectName(QStringLiteral("aioStoreSubtitle"));
  titleStack->addWidget(storeSub);
  hdrLay->addLayout(titleStack);
  hdrLay->addStretch();
  auto *backHint =
      new QLabel(QStringLiteral("[ Esc ] Return home"), headerBar_);
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
  shelfLay->setContentsMargins(48, 24, 48, 16);
  shelfLay->setSpacing(4);
  shelfEyebrow_ = new QLabel(QStringLiteral("DISCOVER"), shelfHeader_);
  shelfEyebrow_->setObjectName(QStringLiteral("aioStoreShelfEyebrow"));
  shelfLay->addWidget(shelfEyebrow_);
  shelfTitle_ = new QLabel(QStringLiteral("Featured Catalog"), shelfHeader_);
  shelfTitle_->setObjectName(QStringLiteral("aioStoreShelfTitle"));
  shelfLay->addWidget(shelfTitle_);
  shelfSummary_ = new QLabel(
      QStringLiteral("Launching a native game catalog and your local library."),
      shelfHeader_);
  shelfSummary_->setObjectName(QStringLiteral("aioStoreShelfSummary"));
  shelfSummary_->setWordWrap(true);
  shelfLay->addWidget(shelfSummary_);
  outer->addWidget(shelfHeader_);

  contentArea_ = new QWidget(this);
  auto *contLay = new QHBoxLayout(contentArea_);
  contLay->setContentsMargins(0, 0, 0, 0);
  contLay->setSpacing(0);
  gridScroll_ = new QScrollArea(this);
  gridScroll_->setWidgetResizable(true);
  gridScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  gridScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  gridScroll_->setFrameShape(QFrame::NoFrame);
  gridHost_ = new QWidget();
  gridScroll_->setWidget(gridHost_);
  contLay->addWidget(gridScroll_, 1);
  // Detail panel
  detailPanel_ = new QFrame(this);
  detailPanel_->setObjectName(QStringLiteral("aioStoreDetailPanel"));
  detailPanel_->setFixedWidth(400);
  detailPanel_->setVisible(false);
  auto *detLay = new QVBoxLayout(detailPanel_);
  detLay->setContentsMargins(24, 24, 24, 24);
  detLay->setSpacing(12);
  detailArt_ = new QLabel(detailPanel_);
  detailArt_->setFixedHeight(180);
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
  detLay->addStretch();
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
  contLay->addWidget(detailPanel_);
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

  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
  updateShelfHeader();
}
void GameStorePage::rebuildGrid() {
  kGridCols = computeGridCols();
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
  grid->setContentsMargins(48, 24, 48, 48);
  grid->setHorizontalSpacing(24);
  grid->setVerticalSpacing(24);

  for (int i = 0; i < filteredGames_.size(); ++i) {
    auto *card = new GameCard(filteredGames_[i], gridHost_);
    grid->addWidget(card, i / kGridCols, i % kGridCols);
    cards_.append(card);
  }
  for (int c = 0; c < kGridCols; ++c)
    grid->setColumnStretch(c, 1);
  gridFocusRow_ = 0;
  gridFocusCol_ = 0;
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
}

void GameStorePage::activateFocusedGame() {
  const int idx = gridFocusRow_ * kGridCols + gridFocusCol_;
  if (idx < 0 || idx >= filteredGames_.size())
    return;
  showDetailPanel(filteredGames_[idx]);
}

void GameStorePage::showDetailPanel(const StoreGame &game) {
  detailVisible_ = true;
  detailPanel_->setVisible(true);
  QPixmap artPm(detailPanel_->width() - 48, 180);
  artPm.fill(Qt::transparent);
  {
    QPainter p(&artPm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, artPm.height());
    g.setColorAt(0.0, game.coverColor.lighter(130));
    g.setColorAt(1.0, game.coverColor.darker(200));
    QPainterPath pp;
    pp.addRoundedRect(QRectF(artPm.rect()), 12, 12);
    p.fillPath(pp, g);

    // Decorative diagonal
    QPainterPath diag;
    const QRectF ar(artPm.rect());
    diag.moveTo(ar.left(), ar.top() + ar.height() * 0.6);
    diag.lineTo(ar.right(), ar.top() + ar.height() * 0.2);
    diag.lineTo(ar.right(), ar.top() + ar.height() * 0.4);
    diag.lineTo(ar.left(), ar.top() + ar.height() * 0.8);
    diag.closeSubpath();
    p.fillPath(diag, QColor(255, 255, 255, 18));

    // Large initial
    QFont initFont;
    initFont.setPixelSize(static_cast<int>(ar.height() * 0.4));
    initFont.setWeight(QFont::Bold);
    p.setFont(initFont);
    p.setPen(QColor(255, 255, 255, 48));
    const QString initial = game.title.isEmpty() ? QStringLiteral("?")
                                                 : game.title.left(1).toUpper();
    p.drawText(ar, Qt::AlignCenter, initial);

    // Title overlay at bottom
    p.setPen(QColor(255, 255, 255, 200));
    QFont f;
    f.setPixelSize(18);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    const QRectF textRect(ar.left() + 16, ar.bottom() - 48, ar.width() - 32,
                          32);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, game.title);
  }
  detailArt_->setPixmap(artPm);
  detailTitle_->setText(game.title);

  if (game.isRomGame) {
    // ROM library game — show console type, no price
    detailPublisher_->setText(game.category);
    detailYear_->setText(QString());
    detailRating_->setText(QString());
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        QStringLiteral("This ROM is in your local games library. "
                       "Press Enter or click below to launch it."));
    installBtn_->setText(QStringLiteral("Launch Game"));
    installBtn_->setObjectName(QStringLiteral("aioStorePlayButton"));
  } else if (game.isOnSale && game.discountPercent > 0 &&
             game.priceUsdCents > 0) {
    const int origCents = static_cast<int>(game.priceUsdCents * 100.0 /
                                           (100 - game.discountPercent));
    detailPublisher_->setText(formatPrice(game.priceUsdCents));
    detailYear_->setText(
        QStringLiteral("(was %1)").arg(formatPrice(origCents)));
    detailRating_->setText(
        QStringLiteral("SAVE %1%  on sale now").arg(game.discountPercent));
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        game.isInstalled
            ? QStringLiteral("This game is in your library. "
                             "Press Enter or click below to launch it.")
            : QStringLiteral("Open the catalog to browse screenshots, "
                             "read reviews, and manage this title."));
    installBtn_->setText(game.isInstalled ? QStringLiteral("Launch Game")
                                          : QStringLiteral("Open in Store"));
    installBtn_->setObjectName(game.isInstalled
                                   ? QStringLiteral("aioStorePlayButton")
                                   : QStringLiteral("aioStoreInstallButton"));
  } else {
    detailPublisher_->setText(formatPrice(game.priceUsdCents));
    detailYear_->setText(QString());
    detailRating_->setText(QString());
    detailDescription_->setTextFormat(Qt::PlainText);
    detailDescription_->setText(
        game.isInstalled
            ? QStringLiteral("This game is in your library. "
                             "Press Enter or click below to launch it.")
            : QStringLiteral("Open the catalog to browse screenshots, "
                             "read reviews, and manage this title."));
    installBtn_->setText(game.isInstalled ? QStringLiteral("Launch Game")
                                          : QStringLiteral("Open in Store"));
    installBtn_->setObjectName(game.isInstalled
                                   ? QStringLiteral("aioStorePlayButton")
                                   : QStringLiteral("aioStoreInstallButton"));
  }
  installBtn_->style()->unpolish(installBtn_);
  installBtn_->style()->polish(installBtn_);
  detailCategory_->setText(game.isRomGame ? QStringLiteral("Local Library")
                                          : game.category);
}

void GameStorePage::clearDetailPanel() {
  detailVisible_ = false;
  detailPanel_->setVisible(false);
  focusArea_ = FocusArea::Grid;
  updateGridFocus();
  updateTabFocus();
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
  updateTabFocus();
  updateGridFocus();
  setFocus();
}
void GameStorePage::keyPressEvent(QKeyEvent *event) {
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
      focusArea_ = FocusArea::Grid;
      updateTabFocus();
      updateGridFocus();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      activeCategoryIndex_ = tabFocus_;
      applyActiveCategoryFilter();
      rebuildGrid();
      focusArea_ = FocusArea::Grid;
      updateTabFocus();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
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
        focusArea_ = FocusArea::Tabs;
        tabFocus_ = activeCategoryIndex_;
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
  }
}

} // namespace AIO::GUI
