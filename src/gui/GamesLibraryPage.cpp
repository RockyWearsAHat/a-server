#include "gui/GamesLibraryPage.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace AIO::GUI {

namespace {

QColor badgeColorFor(const QString &consoleBadge) {
  if (consoleBadge == QStringLiteral("GBA"))
    return QColor(QStringLiteral("#9c8cff"));
  if (consoleBadge == QStringLiteral("PS1"))
    return QColor(QStringLiteral("#64b5f6"));
  return QColor(QStringLiteral("#ff6b6b"));
}

int extensionRank(const QString &path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  if (ext == QStringLiteral("chd"))
    return 7;
  if (ext == QStringLiteral("pbp"))
    return 6;
  if (ext == QStringLiteral("cue"))
    return 5;
  if (ext == QStringLiteral("iso"))
    return 4;
  if (ext == QStringLiteral("img"))
    return 3;
  if (ext == QStringLiteral("bin"))
    return 2;
  return 1;
}

QString platformSummary(const LibraryGame &game) {
  if (game.consoleBadge == QStringLiteral("GBA"))
    return QStringLiteral("Game Boy Advance ROM ready to launch");
  if (game.consoleBadge == QStringLiteral("PS1"))
    return QStringLiteral("PlayStation ROM ready to launch");
  return QStringLiteral("Nintendo Switch library entry detected");
}

QString supportSummary(const LibraryGame &game) {
  if (game.unsupported) {
    return QStringLiteral(
        "This title is indexed so your library stays organized, but Switch "
        "launch is intentionally unavailable in the production shell.");
  }
  return QStringLiteral(
      "Open the game card for a clean launch flow and metadata summary.");
}

QString formatSummary(const LibraryGame &game) {
  return QFileInfo(game.path).suffix().toUpper();
}

QString displayTitleFor(const QString &title) {
  QString normalized = title;
  normalized.replace(QChar('_'), QChar(' '));
  normalized.replace(QChar('-'), QChar(' '));
  normalized.replace(QRegularExpression(QStringLiteral("([a-z0-9])([A-Z])")),
                     QStringLiteral("\\1 \\2"));
  normalized.replace(QRegularExpression(QStringLiteral("\\s+")),
                     QStringLiteral(" "));
  return normalized.trimmed();
}

QPixmap renderLibraryArtwork(const LibraryGame &game, const QSize &size) {
  QPixmap pixmap(size);
  pixmap.fill(Qt::transparent);

  const QColor accent = badgeColorFor(game.consoleBadge);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  QPainterPath cardPath;
  cardPath.addRoundedRect(QRectF(pixmap.rect()), 16.0, 16.0);
  QLinearGradient gradient(0.0, 0.0, static_cast<qreal>(size.width()),
                           static_cast<qreal>(size.height()));
  gradient.setColorAt(0.0, accent.darker(210));
  gradient.setColorAt(0.6, QColor(QStringLiteral("#1a1a1a")));
  gradient.setColorAt(1.0, QColor(QStringLiteral("#0a0a0a")));
  painter.fillPath(cardPath, gradient);

  QLinearGradient sheen(0.0, 0.0, 0.0, static_cast<qreal>(size.height()));
  sheen.setColorAt(0.0, QColor(255, 255, 255, 28));
  sheen.setColorAt(0.45, QColor(255, 255, 255, 0));
  sheen.setColorAt(1.0, QColor(0, 0, 0, 72));
  painter.fillPath(cardPath, sheen);

  painter.setPen(QPen(QColor(255, 255, 255, 10), 1.0));
  painter.drawPath(cardPath);

  QFont badgeFont(QStringLiteral("Noto Sans"));
  badgeFont.setPixelSize(12);
  badgeFont.setWeight(QFont::DemiBold);
  painter.setFont(badgeFont);

  const QRectF badgeRect(24.0, 24.0, 88.0, 28.0);
  QPainterPath badgePath;
  badgePath.addRoundedRect(badgeRect, 6.0, 6.0);
  painter.fillPath(badgePath,
                   QColor(accent.red(), accent.green(), accent.blue(), 60));
  painter.setPen(accent.lighter(135));
  painter.drawText(badgeRect, Qt::AlignCenter, game.consoleBadge);

  if (game.unsupported) {
    const QRectF unsupportedRect(size.width() - 152.0, 24.0, 128.0, 28.0);
    QPainterPath unsupportedPath;
    unsupportedPath.addRoundedRect(unsupportedRect, 6.0, 6.0);
    painter.fillPath(unsupportedPath, QColor(255, 107, 107, 210));
    painter.setPen(QColor(QStringLiteral("#0a0a0a")));
    painter.drawText(unsupportedRect, Qt::AlignCenter,
                     QStringLiteral("Unsupported"));
  }

  const QRectF artRect(pixmap.rect());
  const QRectF titlePlate(artRect.left() + 24.0, artRect.top() + 88.0,
                          artRect.width() - 48.0, artRect.height() - 152.0);
  QPainterPath titlePlatePath;
  titlePlatePath.addRoundedRect(titlePlate, 16.0, 16.0);
  painter.fillPath(titlePlatePath, QColor(10, 10, 10, 86));

  QFont titleFont(QStringLiteral("Noto Sans"));
  titleFont.setPixelSize(32);
  titleFont.setWeight(QFont::Bold);
  painter.setFont(titleFont);
  painter.setPen(QColor(QStringLiteral("#f0f0f0")));
  painter.drawText(titlePlate.toRect().adjusted(24, 24, -24, -56),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   displayTitleFor(game.title));

  QFont metaFont(QStringLiteral("Noto Sans"));
  metaFont.setPixelSize(13);
  painter.setFont(metaFont);
  painter.setPen(QColor(QStringLiteral("#999999")));
  painter.drawText(
      QRect(24, size.height() - 48, size.width() - 48, 20),
      Qt::AlignLeft | Qt::AlignVCenter,
      QStringLiteral("Local library  •  %1").arg(formatSummary(game)));

  return pixmap;
}

class LibraryInfoDialog final : public QDialog {
public:
  explicit LibraryInfoDialog(const LibraryGame &game, QWidget *parent = nullptr)
      : QDialog(parent) {
    setModal(true);
    setObjectName(QStringLiteral("aioGamesInfoDialog"));
    resize(720, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    auto *art = new QLabel(this);
    art->setObjectName(QStringLiteral("aioGamesDialogArt"));
    art->setPixmap(renderLibraryArtwork(game, QSize(656, 264)));
    art->setFixedHeight(264);
    art->setAlignment(Qt::AlignCenter);
    layout->addWidget(art);

    auto *title = new QLabel(displayTitleFor(game.title), this);
    title->setObjectName(QStringLiteral("aioGamesDialogTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);

    auto *meta = new QLabel(QStringLiteral("%1  •  %2  •  %3")
                                .arg(game.consoleBadge, formatSummary(game),
                                     game.unsupported
                                         ? QStringLiteral("Unavailable")
                                         : QStringLiteral("Ready to launch")),
                            this);
    meta->setObjectName(QStringLiteral("aioGamesDialogMeta"));
    layout->addWidget(meta);

    auto *body = new QLabel(supportSummary(game), this);
    body->setObjectName(QStringLiteral("aioGamesDialogBody"));
    body->setWordWrap(true);
    layout->addWidget(body);

    auto *pathLabel = new QLabel(game.path, this);
    pathLabel->setObjectName(QStringLiteral("aioGamesDialogPath"));
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(this);
    buttons->setCenterButtons(false);
    auto *closeButton = buttons->addButton(QStringLiteral("Close"),
                                           QDialogButtonBox::RejectRole);
    closeButton->setObjectName(QStringLiteral("aioGamesSecondaryAction"));
    auto *launchButton =
        buttons->addButton(game.unsupported ? QStringLiteral("Unavailable")
                                            : QStringLiteral("Launch Game"),
                           QDialogButtonBox::AcceptRole);
    launchButton->setObjectName(QStringLiteral("aioGamesPrimaryAction"));
    launchButton->setEnabled(!game.unsupported);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
  }
};

} // namespace

GamesLibraryPage::GamesLibraryPage(QWidget *parent) : QWidget(parent) {
  setupUi();
  refresh();
}

void GamesLibraryPage::setupUi() {
  setObjectName(QStringLiteral("aioGamesLibraryPage"));
  setFocusPolicy(Qt::StrongFocus);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto *header = new QWidget(this);
  header->setObjectName(QStringLiteral("aioGamesLibraryHeader"));
  auto *headerLayout = new QVBoxLayout(header);
  headerLayout->setContentsMargins(32, 24, 32, 16);
  headerLayout->setSpacing(8);

  auto *titleLabel = new QLabel(QStringLiteral("Games Library"), header);
  titleLabel->setObjectName(QStringLiteral("aioGamesLibraryTitle"));
  headerLayout->addWidget(titleLabel);

  subtitleLabel_ = new QLabel(
      QStringLiteral("Browse your installed titles from a clean, fast rail."),
      header);
  subtitleLabel_->setObjectName(QStringLiteral("aioGamesLibrarySubtitle"));
  headerLayout->addWidget(subtitleLabel_);
  root->addWidget(header);

  auto *body = new QWidget(this);
  body->setObjectName(QStringLiteral("aioGamesLibraryBody"));
  auto *bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(32, 0, 32, 32);
  bodyLayout->setSpacing(24);

  auto *railPanel = new QFrame(body);
  railPanel->setObjectName(QStringLiteral("aioGamesRailPanel"));
  railPanel->setFixedWidth(320);
  auto *railLayout = new QVBoxLayout(railPanel);
  railLayout->setContentsMargins(24, 24, 24, 24);
  railLayout->setSpacing(16);

  auto *railHeading = new QLabel(QStringLiteral("Installed Titles"), railPanel);
  railHeading->setObjectName(QStringLiteral("aioGamesRailHeading"));
  railLayout->addWidget(railHeading);

  filterBar_ = new QWidget(railPanel);
  filterBar_->setObjectName(QStringLiteral("aioGamesFilterBar"));
  auto *chipGrid = new QGridLayout(filterBar_);
  chipGrid->setContentsMargins(0, 0, 0, 0);
  chipGrid->setHorizontalSpacing(8);
  chipGrid->setVerticalSpacing(8);
  chipGrid->setColumnStretch(0, 1);
  chipGrid->setColumnStretch(1, 1);

  const QVector<QString> labels = {QStringLiteral("All"), QStringLiteral("GBA"),
                                   QStringLiteral("PS1"),
                                   QStringLiteral("Switch")};
  for (int i = 0; i < labels.size(); ++i) {
    auto *chip = new QLabel(labels[i], filterBar_);
    chip->setObjectName(QStringLiteral("aioGamesFilterChip"));
    chip->setAlignment(Qt::AlignCenter);
    chips_.append(chip);
    chipGrid->addWidget(chip, i / 2, i % 2);
  }
  railLayout->addWidget(filterBar_);

  titleList_ = new QListWidget(railPanel);
  titleList_->setObjectName(QStringLiteral("aioGamesTitleRail"));
  titleList_->setFrameShape(QFrame::NoFrame);
  titleList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  titleList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  titleList_->setSelectionMode(QAbstractItemView::SingleSelection);
  titleList_->setFocusPolicy(Qt::NoFocus);
  titleList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  railLayout->addWidget(titleList_, 1);

  auto *hintLabel = new QLabel(
      QStringLiteral("Enter opens details  •  Right moves to actions"),
      railPanel);
  hintLabel->setObjectName(QStringLiteral("aioGamesRailHint"));
  hintLabel->setWordWrap(true);
  railLayout->addWidget(hintLabel);
  bodyLayout->addWidget(railPanel);

  auto *detailPanel = new QWidget(body);
  detailPanel->setObjectName(QStringLiteral("aioGamesDetailsPane"));
  auto *detailLayout = new QVBoxLayout(detailPanel);
  detailLayout->setContentsMargins(0, 16, 0, 0);
  detailLayout->setSpacing(24);

  auto *heroCard = new QFrame(detailPanel);
  heroCard->setObjectName(QStringLiteral("aioGamesHeroCard"));
  auto *heroLayout = new QVBoxLayout(heroCard);
  heroLayout->setContentsMargins(24, 24, 24, 24);
  heroLayout->setSpacing(16);

  heroArt_ = new QLabel(heroCard);
  heroArt_->setObjectName(QStringLiteral("aioGamesHeroArt"));
  heroArt_->setAlignment(Qt::AlignCenter);
  heroArt_->setFixedHeight(320);
  heroLayout->addWidget(heroArt_);

  heroBadge_ = new QLabel(heroCard);
  heroBadge_->setObjectName(QStringLiteral("aioGamesHeroBadge"));
  heroBadge_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  heroLayout->addWidget(heroBadge_);

  heroTitle_ = new QLabel(heroCard);
  heroTitle_->setObjectName(QStringLiteral("aioGamesHeroTitle"));
  heroTitle_->setWordWrap(true);
  heroLayout->addWidget(heroTitle_);

  heroSubtitle_ = new QLabel(heroCard);
  heroSubtitle_->setObjectName(QStringLiteral("aioGamesHeroSubtitle"));
  heroSubtitle_->setWordWrap(true);
  heroLayout->addWidget(heroSubtitle_);

  heroDescription_ = new QLabel(heroCard);
  heroDescription_->setObjectName(QStringLiteral("aioGamesHeroDescription"));
  heroDescription_->setWordWrap(true);
  heroLayout->addWidget(heroDescription_);
  detailLayout->addWidget(heroCard);

  auto *statsRow = new QHBoxLayout();
  statsRow->setSpacing(16);
  const struct {
    const char *title;
    QLabel **valueLabel;
  } statDefs[] = {{"Platform", &platformValueLabel_},
                  {"Status", &statusValueLabel_},
                  {"Format", &formatValueLabel_}};
  for (const auto &statDef : statDefs) {
    auto *card = new QFrame(detailPanel);
    card->setObjectName(QStringLiteral("aioGamesStatCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(8);
    auto *eyebrow = new QLabel(QString::fromUtf8(statDef.title), card);
    eyebrow->setObjectName(QStringLiteral("aioGamesStatEyebrow"));
    auto *value = new QLabel(card);
    value->setObjectName(QStringLiteral("aioGamesStatValue"));
    value->setWordWrap(true);
    cardLayout->addWidget(eyebrow);
    cardLayout->addWidget(value);
    statsRow->addWidget(card, 1);
    *statDef.valueLabel = value;
  }
  detailLayout->addLayout(statsRow);

  auto *actionsRow = new QHBoxLayout();
  actionsRow->setSpacing(16);
  launchButton_ = new QPushButton(QStringLiteral("Launch Game"), detailPanel);
  launchButton_->setObjectName(QStringLiteral("aioGamesPrimaryAction"));
  launchButton_->setFocusPolicy(Qt::NoFocus);
  actionsRow->addWidget(launchButton_, 1);

  infoButton_ = new QPushButton(QStringLiteral("View Details"), detailPanel);
  infoButton_->setObjectName(QStringLiteral("aioGamesSecondaryAction"));
  infoButton_->setFocusPolicy(Qt::NoFocus);
  actionsRow->addWidget(infoButton_, 1);
  detailLayout->addLayout(actionsRow);

  emptyLabel_ = new QLabel(
      QStringLiteral("No games found. Add ROMs in Settings to populate your "
                     "library."),
      detailPanel);
  emptyLabel_->setObjectName(QStringLiteral("aioGamesEmptyLabel"));
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setWordWrap(true);
  detailLayout->addWidget(emptyLabel_);
  detailLayout->addStretch();
  bodyLayout->addWidget(detailPanel, 1);
  root->addWidget(body, 1);

  connect(titleList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0 || row >= displayGames_.size())
      return;
    gridRow_ = row;
    updateGridFocus();
  });
  connect(titleList_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem *) { openDetailsDialog(); });
  connect(launchButton_, &QPushButton::clicked, this,
          [this]() { activateFocused(); });
  connect(infoButton_, &QPushButton::clicked, this,
          [this]() { openDetailsDialog(); });

  updateFilterChips();
  updateGridFocus();
}

void GamesLibraryPage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  setFocus();
}

void GamesLibraryPage::refresh() {
  scanROMs();
  rebuildGrid();
}

void GamesLibraryPage::scanROMs() {
  allGames_.clear();

  QSettings settings(QStringLiteral("AIOServer"),
                     QStringLiteral("GBAEmulator"));
  const QString romDir =
      settings.value(QStringLiteral("romDirectory"), QDir::homePath())
          .toString();
  const QDir dir(romDir);
  if (!dir.exists())
    return;

  QMap<QString, LibraryGame> uniqueGames;
  const QStringList filters = {QStringLiteral("*.gba"), QStringLiteral("*.bin"),
                               QStringLiteral("*.cue"), QStringLiteral("*.iso"),
                               QStringLiteral("*.img"), QStringLiteral("*.chd"),
                               QStringLiteral("*.pbp"), QStringLiteral("*.xci"),
                               QStringLiteral("*.nsp"), QStringLiteral("*.nso"),
                               QStringLiteral("*.nro")};
  QDirIterator it(romDir, filters, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const QString ext = fi.suffix().toLower();

    LibraryGame game;
    game.path = fi.absoluteFilePath();
    game.title = fi.completeBaseName();

    if (ext == QStringLiteral("gba")) {
      game.consoleBadge = QStringLiteral("GBA");
    } else if (ext == QStringLiteral("bin") || ext == QStringLiteral("cue") ||
           ext == QStringLiteral("iso") || ext == QStringLiteral("img") ||
           ext == QStringLiteral("chd") || ext == QStringLiteral("pbp")) {
      game.consoleBadge = QStringLiteral("PS1");
    } else if (ext == QStringLiteral("xci") || ext == QStringLiteral("nsp") ||
               ext == QStringLiteral("nso") || ext == QStringLiteral("nro")) {
      game.consoleBadge = QStringLiteral("Switch");
      game.unsupported = true;
    } else {
      continue;
    }

    const QString key = QStringLiteral("%1::%2").arg(
        game.consoleBadge, game.title.trimmed().toLower());
    const auto existing = uniqueGames.constFind(key);
    if (existing == uniqueGames.cend() ||
        extensionRank(existing->path) < extensionRank(game.path)) {
      uniqueGames.insert(key, game);
    }
  }

  allGames_ = uniqueGames.values().toVector();
  std::sort(allGames_.begin(), allGames_.end(),
            [](const LibraryGame &a, const LibraryGame &b) {
              return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
}

void GamesLibraryPage::rebuildGrid() {
  displayGames_.clear();

  auto matchesFilter = [this](const LibraryGame &game) {
    switch (filter_) {
    case FilterMode::All:
      return true;
    case FilterMode::GBA:
      return game.consoleBadge == QStringLiteral("GBA");
    case FilterMode::PS1:
      return game.consoleBadge == QStringLiteral("PS1");
    case FilterMode::Switch:
      return game.consoleBadge == QStringLiteral("Switch");
    }
    return true;
  };

  for (const auto &game : allGames_) {
    if (matchesFilter(game))
      displayGames_.append(game);
  }

  if (titleList_) {
    QSignalBlocker blocker(titleList_);
    titleList_->clear();
    for (const auto &game : displayGames_) {
      auto *item = new QListWidgetItem(displayTitleFor(game.title), titleList_);
      item->setIcon(QIcon(renderLibraryArtwork(game, QSize(80, 80))));
      item->setData(Qt::UserRole, game.consoleBadge);
      item->setData(Qt::UserRole + 1, game.unsupported);
      item->setToolTip(game.path);
    }
  }

  subtitleLabel_->setText(
      QStringLiteral("%1 titles ready to browse").arg(displayGames_.size()));

  if (displayGames_.isEmpty()) {
    gridRow_ = 0;
    gridCol_ = 0;
    focusArea_ = FocusArea::Filters;
    inChips_ = true;
    emptyLabel_->show();
  } else {
    const int clampedIndex = qBound(0, gridRow_, displayGames_.size() - 1);
    gridRow_ = clampedIndex;
    if (titleList_)
      titleList_->setCurrentRow(clampedIndex);
    if (focusArea_ == FocusArea::Filters)
      focusArea_ = FocusArea::Titles;
    inChips_ = false;
    emptyLabel_->hide();
  }

  updateFilterChips();
  updateGridFocus();
}

void GamesLibraryPage::updateFilterChips() {
  const int active = static_cast<int>(filter_);
  inChips_ = focusArea_ == FocusArea::Filters;
  for (int i = 0; i < chips_.size(); ++i) {
    const char *activeVal = (i == active) ? "true" : "false";
    const char *focusedVal =
        (focusArea_ == FocusArea::Filters && i == chipFocus_) ? "true"
                                                              : "false";
    if (chips_[i]->property("active").toString() == QLatin1String(activeVal) &&
        chips_[i]->property("focused").toString() == QLatin1String(focusedVal))
      continue;
    chips_[i]->setProperty("active", activeVal);
    chips_[i]->setProperty("focused", focusedVal);
    chips_[i]->style()->unpolish(chips_[i]);
    chips_[i]->style()->polish(chips_[i]);
  }
}

void GamesLibraryPage::updateGridFocus() {
  if (titleList_ && !displayGames_.isEmpty()) {
    const QSignalBlocker blocker(titleList_);
    titleList_->setCurrentRow(selectedIndex());
    titleList_->scrollToItem(titleList_->currentItem(),
                             QAbstractItemView::PositionAtCenter);
  }

  const bool actionsFocused = focusArea_ == FocusArea::Actions;
  gridCol_ = actionsFocused ? actionFocus_ : 0;

  if (launchButton_) {
    const char *launchVal =
        (actionsFocused && actionFocus_ == 0) ? "true" : "false";
    if (launchButton_->property("aio_selected").toString() !=
        QLatin1String(launchVal)) {
      launchButton_->setProperty("aio_selected", launchVal);
      launchButton_->style()->unpolish(launchButton_);
      launchButton_->style()->polish(launchButton_);
    }
  }
  if (infoButton_) {
    const char *infoVal =
        (actionsFocused && actionFocus_ == 1) ? "true" : "false";
    if (infoButton_->property("aio_selected").toString() !=
        QLatin1String(infoVal)) {
      infoButton_->setProperty("aio_selected", infoVal);
      infoButton_->style()->unpolish(infoButton_);
      infoButton_->style()->polish(infoButton_);
    }
  }

  updateHeroPanel();
}

void GamesLibraryPage::updateHeroPanel() {
  const bool hasSelection = !displayGames_.isEmpty();
  const LibraryGame game =
      hasSelection ? displayGames_.at(selectedIndex()) : LibraryGame{};

  if (!hasSelection) {
    heroArt_->setPixmap(QPixmap());
    heroBadge_->setText(QStringLiteral("Library Empty"));
    heroTitle_->setText(QStringLiteral("Add ROMs to start browsing"));
    heroSubtitle_->setText(
        QStringLiteral("Your title rail will populate here."));
    heroDescription_->setText(
        QStringLiteral("Point the ROM directory at your local collection, and "
                       "the library will group everything into a faster Steam-"
                       "style navigation flow."));
    platformValueLabel_->setText(QStringLiteral("None"));
    statusValueLabel_->setText(QStringLiteral("Waiting"));
    formatValueLabel_->setText(QStringLiteral("N/A"));
    launchButton_->setEnabled(false);
    infoButton_->setEnabled(false);
    return;
  }

  heroArt_->setPixmap(renderLibraryArtwork(game, QSize(820, 320)));
  heroBadge_->hide();
  heroTitle_->setText(displayTitleFor(game.title));
  heroSubtitle_->setText(
      game.unsupported
          ? QStringLiteral("Indexed but unavailable to launch")
          : QStringLiteral("Ready to launch from your local library"));
  heroDescription_->setText(supportSummary(game));
  platformValueLabel_->setText(game.consoleBadge);
  statusValueLabel_->setText(game.unsupported ? QStringLiteral("Unavailable")
                                              : QStringLiteral("Installed"));
  formatValueLabel_->setText(formatSummary(game));
  launchButton_->setEnabled(!game.unsupported);
  launchButton_->setText(game.unsupported ? QStringLiteral("Unavailable")
                                          : QStringLiteral("Launch Game"));
  infoButton_->setEnabled(true);
}

int GamesLibraryPage::selectedIndex() const {
  if (displayGames_.isEmpty())
    return 0;
  return qBound(0, gridRow_, displayGames_.size() - 1);
}

void GamesLibraryPage::activateFocused() {
  if (displayGames_.isEmpty())
    return;
  const auto &game = displayGames_.at(selectedIndex());
  if (game.unsupported)
    return;
  emit gameSelected(game.path);
}

void GamesLibraryPage::openDetailsDialog() {
  if (displayGames_.isEmpty())
    return;

  const auto &game = displayGames_.at(selectedIndex());
  LibraryInfoDialog dialog(game, this);
  if (dialog.exec() == QDialog::Accepted && !game.unsupported)
    emit gameSelected(game.path);
}

void GamesLibraryPage::keyPressEvent(QKeyEvent *event) {
  const int key = event->key();
  const int size = displayGames_.size();

  if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
    emit backRequested();
    return;
  }

  switch (focusArea_) {
  case FocusArea::Filters:
    if (key == Qt::Key_Left && chipFocus_ > 0) {
      --chipFocus_;
      updateFilterChips();
      return;
    }
    if (key == Qt::Key_Right && chipFocus_ + 1 < chips_.size()) {
      ++chipFocus_;
      updateFilterChips();
      return;
    }
    if (key == Qt::Key_Down && size > 0) {
      focusArea_ = FocusArea::Titles;
      updateFilterChips();
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      filter_ = static_cast<FilterMode>(chipFocus_);
      rebuildGrid();
      return;
    }
    break;
  case FocusArea::Titles:
    if (size == 0) {
      focusArea_ = FocusArea::Filters;
      updateFilterChips();
      return;
    }
    if (key == Qt::Key_Up) {
      if (gridRow_ == 0) {
        focusArea_ = FocusArea::Filters;
        chipFocus_ = static_cast<int>(filter_);
        updateFilterChips();
      } else {
        --gridRow_;
        updateGridFocus();
      }
      return;
    }
    if (key == Qt::Key_Down && gridRow_ + 1 < size) {
      ++gridRow_;
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Right) {
      focusArea_ = FocusArea::Actions;
      actionFocus_ = 0;
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      openDetailsDialog();
      return;
    }
    break;
  case FocusArea::Actions:
    if (key == Qt::Key_Left) {
      focusArea_ = FocusArea::Titles;
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Right || key == Qt::Key_Down) {
      actionFocus_ = qMin(1, actionFocus_ + 1);
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Up) {
      actionFocus_ = qMax(0, actionFocus_ - 1);
      updateGridFocus();
      return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      if (actionFocus_ == 0)
        activateFocused();
      else
        openDetailsDialog();
      return;
    }
    break;
  }

  QWidget::keyPressEvent(event);
}

} // namespace AIO::GUI
