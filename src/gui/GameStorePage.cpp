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
    setFixedSize(200, 240);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    titleLabel_ = new QLabel(game.title, this);
    titleLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont tf;
    tf.setPixelSize(14);
    tf.setWeight(QFont::Bold);
    titleLabel_->setFont(tf);
    titleLabel_->setStyleSheet(
        QStringLiteral("color: #ffffff; padding: 4px 10px 2px 10px;"));
    const QString priceStr =
        game.isRomGame
            ? game.category
            : ((game.isOnSale && game.discountPercent > 0)
                   ? QStringLiteral("-%1% off").arg(game.discountPercent)
                   : formatPrice(game.priceUsdCents));
    pubLabel_ = new QLabel(priceStr, this);
    pubLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QFont pf;
    pf.setPixelSize(12);
    pubLabel_->setFont(pf);
    pubLabel_->setStyleSheet(
        game.isRomGame
            ? QStringLiteral("color: #9c8cff; padding: 0 10px 8px 10px;")
            : ((game.isOnSale && game.discountPercent > 0)
                   ? QStringLiteral("color: #4caf50; padding: 0 10px 8px 10px;")
                   : QStringLiteral(
                         "color: #999999; padding: 0 10px 8px 10px;")));
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
    const qreal radius = 12.0;
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0.0, game_.coverColor.lighter(145));
    grad.setColorAt(1.0, game_.coverColor.darker(138));
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
          QStringLiteral("-%1%%").arg(game_.discountPercent);
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
      instFont.setPixelSize(12);
      instFont.setWeight(QFont::Medium);
      p.setFont(instFont);
      const QFontMetrics fm(instFont);
      const int tw = fm.horizontalAdvance(installedText);
      const QRectF instRect(r.left() + 8, r.bottom() - 28, tw + 16, 20);
      QPainterPath instPath;
      instPath.addRoundedRect(instRect, 6, 6);
      p.fillPath(instPath, QColor(63, 185, 80, 200));
      p.setPen(QColor(0, 0, 0, 220));
      p.drawText(instRect, Qt::AlignCenter, installedText);
    }

    // Focus ring — crisp 3px white border at card boundary (tvOS/PS5 style)
    if (property("aio_selected").toBool()) {
      QPainterPath ring;
      ring.addRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1.5,
                          radius - 1.5);
      p.setPen(QPen(QColor(255, 255, 255, 224), 3.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(ring);
    }

    // Source chip — small badge top-left showing origin (Steam or console)
    {
      const QString srcText =
          game_.isRomGame ? game_.category
    : (game_.sourceLabel.isEmpty() ? QStringLiteral("CATALOG") : game_.sourceLabel);
      QFont srcFont;
      srcFont.setPixelSize(12);
      srcFont.setWeight(QFont::DemiBold);
      p.setFont(srcFont);
      const QFontMetrics sfm(srcFont);
      const int sw = sfm.horizontalAdvance(srcText) + 12;
      const QRectF srcRect(r.left() + 8, r.top() + 8, sw, 18);
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
  // No static catalog load — data comes from SteamService via setSteamService()
  // Populate minimal "All" category so the tab bar renders before data arrives
  categories_.append(QStringLiteral("All"));
  setupUi();
}

void GameStorePage::setSteamService(SteamService *service) {
  steamService_ = service;
  connect(steamService_, &SteamService::gamesReady, this,
          &GameStorePage::onSteamGamesReady);
  connect(steamService_, &SteamService::fetchError, this,
          &GameStorePage::showSteamError);
}

void GameStorePage::onSteamGamesReady(const QList<AIO::GUI::SteamGame> &games) {
  errorShown_ = false;
  if (loadingLabel_)
    loadingLabel_->hide();

  if (steamService_ && !steamService_->isSteamInstalled()) {
    showSteamError(QStringLiteral("Game catalog is unavailable on this system."));
    return;
  }

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
  // My Library is always the last tab
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
    g.sourceLabel = QStringLiteral("CATALOG");
    allGames_.append(g);
  }

  activeCategoryIndex_ = 0;
  tabFocus_ = 0;
  filteredGames_ = allGames_;
  scanLibrary();
  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
}

void GameStorePage::showSteamError(const QString &msg) {
  if (errorShown_)
    return;
  errorShown_ = true;
  if (loadingLabel_)
    loadingLabel_->hide();
  // Clear the grid and show an error message in its place
  for (auto *c : cards_)
    delete c;
  cards_.clear();
  if (gridHost_) {
    delete gridHost_->layout();
    auto *errLay = new QVBoxLayout(gridHost_);
    errLay->setContentsMargins(48, 64, 48, 64);
    errLay->setAlignment(Qt::AlignCenter);
    auto *errCard = new QFrame(gridHost_);
    errCard->setObjectName(QStringLiteral("aioStoreErrorCard"));
    auto *errCardLay = new QVBoxLayout(errCard);
    errCardLay->setContentsMargins(32, 32, 32, 32);
    errCardLay->setSpacing(12);
    auto *icon = new QLabel(QStringLiteral("⚠"), errCard);
    icon->setAlignment(Qt::AlignCenter);
    QFont iconFont;
    iconFont.setPixelSize(48);
    icon->setFont(iconFont);
    icon->setStyleSheet(QStringLiteral("color: rgba(220,50,50,0.8);"));
    auto *errMsg = new QLabel(msg, errCard);
    errMsg->setAlignment(Qt::AlignCenter);
    errMsg->setWordWrap(true);
    auto *backBtn = new QPushButton(QStringLiteral("Return to Home"), errCard);
    backBtn->setObjectName(QStringLiteral("aioStoreInstallButton"));
    connect(backBtn, &QPushButton::clicked, this,
            &GameStorePage::homeRequested);
    errCardLay->addWidget(icon);
    errCardLay->addWidget(errMsg);
    errCardLay->addSpacing(12);
    errCardLay->addWidget(backBtn, 0, Qt::AlignCenter);
    errLay->addStretch();
    errLay->addWidget(errCard, 0, Qt::AlignCenter);
    errLay->addStretch();
  }
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
  // Header
  headerBar_ = new QWidget(this);
  headerBar_->setObjectName(QStringLiteral("aioStoreHeader"));
  auto *hdrLay = new QHBoxLayout(headerBar_);
  hdrLay->setContentsMargins(32, 12, 32, 12);
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
      QStringLiteral("Browse & discover games  ·  Your catalog & local library"),
      headerBar_);
  storeSub->setObjectName(QStringLiteral("aioStoreSubtitle"));
  titleStack->addWidget(storeSub);
  hdrLay->addLayout(titleStack);
  hdrLay->addStretch();
  auto *backHint =
      new QLabel(QStringLiteral("[ Esc ] Return home"), headerBar_);
  backHint->setStyleSheet(
      QStringLiteral("color: rgba(255,255,255,0.3); font-size: 13px;"));
  hdrLay->addWidget(backHint);
  outer->addWidget(headerBar_);
  // Tab bar
  tabBar_ = new QWidget(this);
  tabBar_->setObjectName(QStringLiteral("aioStoreTabBar"));
  auto *tabLay = new QHBoxLayout(tabBar_);
  tabLay->setContentsMargins(32, 0, 32, 0);
  tabLay->setSpacing(4);
  for (const QString &cat : categories_) {
    auto *lbl = new QLabel(cat, tabBar_);
    lbl->setObjectName(QStringLiteral("aioStoreTabLabel"));
    lbl->setAlignment(Qt::AlignCenter);
    tabLay->addWidget(lbl);
    tabLabels_.append(lbl);
  }
  tabLay->addStretch();
  outer->addWidget(tabBar_);
  // Content area
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
  detailPanel_->setFixedWidth(300);
  detailPanel_->setVisible(false);
  auto *detLay = new QVBoxLayout(detailPanel_);
  detLay->setContentsMargins(16, 20, 16, 20);
  detLay->setSpacing(10);
  detailArt_ = new QLabel(detailPanel_);
  detailArt_->setFixedHeight(120);
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
  detailRating_->setStyleSheet(
      QStringLiteral("color: #d49c1c; font-size: 16px; font-weight: 700;"));
  detLay->addWidget(detailRating_);
  detailCategory_ = new QLabel(detailPanel_);
  detailCategory_->setObjectName(QStringLiteral("aioStoreDetailMeta"));
  detailCategory_->setStyleSheet(QStringLiteral(
      "color: #64b5f6; background: rgba(100,181,246,0.12); border: 1px solid "
      "rgba(100,181,246,0.3); border-radius: 10px; padding: 3px 10px; "
      "font-size: 12px; font-weight: 600;"));
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

  // Loading placeholder shown until SteamService delivers data
  loadingOverlay_ = new QWidget(gridHost_);
  auto *loadingLay = new QVBoxLayout(loadingOverlay_);
  loadingLay->setAlignment(Qt::AlignCenter);
  loadingLabel_ =
      new QLabel(QStringLiteral("Loading catalog…"), loadingOverlay_);
  loadingLabel_->setAlignment(Qt::AlignCenter);
  loadingLabel_->setStyleSheet(
      QStringLiteral("color: #999999; font-size: 16px;"));
  loadingLay->addWidget(loadingLabel_);

  rebuildGrid();
  updateTabFocus();
  updateGridFocus();
}
void GameStorePage::rebuildGrid() {
  kGridCols = computeGridCols();
  for (auto *c : cards_)
    delete c;
  cards_.clear();
  if (!gridHost_)
    return;
  delete gridHost_->layout();
  auto *grid = new QGridLayout(gridHost_);
  grid->setContentsMargins(24, 16, 24, 24);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(12);

  if (filteredGames_.isEmpty() && loadingLabel_) {
    loadingLabel_->show();
  } else if (loadingLabel_) {
    loadingLabel_->hide();
  }

  for (int i = 0; i < filteredGames_.size(); ++i) {
    auto *card = new GameCard(filteredGames_[i], gridHost_);
    grid->addWidget(card, i / kGridCols, i % kGridCols);
    cards_.append(card);
  }
  for (int c = 0; c < kGridCols; ++c)
    grid->setColumnStretch(c, 0);
  gridFocusRow_ = 0;
  gridFocusCol_ = 0;
  updateGridFocus();
}

int GameStorePage::colsInGrid() const { return kGridCols; }
int GameStorePage::rowsInGrid() const {
  return cards_.isEmpty() ? 0 : (cards_.size() - 1) / kGridCols + 1;
}

void GameStorePage::updateTabFocus() {
  for (int i = 0; i < tabLabels_.size(); ++i) {
    const bool sel = (focusArea_ == FocusArea::Tabs && i == tabFocus_);
    tabLabels_[i]->setProperty("aio_selected", sel ? "true" : "false");
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
  QPixmap artPm(detailPanel_->width() - 32, 120);
  artPm.fill(Qt::transparent);
  {
    QPainter p(&artPm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, artPm.height());
    g.setColorAt(0.0, game.coverColor);
    g.setColorAt(1.0, game.coverColor.darker(220));
    QPainterPath pp;
    pp.addRoundedRect(QRectF(artPm.rect()), 8, 8);
    p.fillPath(pp, g);
    p.setPen(QColor(255, 255, 255, 200));
    QFont f;
    f.setPixelSize(16);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.drawText(artPm.rect(), Qt::AlignCenter, game.title);
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
    installBtn_->setText(game.isInstalled
                             ? QStringLiteral("Launch Game")
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
    installBtn_->setText(game.isInstalled
                             ? QStringLiteral("Launch Game")
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
  focusArea_ = FocusArea::Grid;
  detailVisible_ = false;
  if (detailPanel_)
    detailPanel_->hide();
  gridFocusRow_ = 0;
  gridFocusCol_ = 0;
  scanLibrary();
  const int newCols = computeGridCols();
  if (newCols != kGridCols)
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
      const QString &cat = categories_[activeCategoryIndex_];
      if (cat == QStringLiteral("My Library")) {
        filteredGames_ = libraryGames_;
      } else if (cat == QStringLiteral("All")) {
        filteredGames_ = allGames_;
      } else {
        filteredGames_.clear();
        for (const auto &g : allGames_)
          if (g.category == cat)
            filteredGames_.append(g);
      }
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
