#include "gui/GamesLibraryPage.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace AIO::GUI {

namespace {
QColor badgeColorFor(const QString &consoleBadge) {
  if (consoleBadge == QStringLiteral("GBA"))
    return QColor(156, 89, 230);
  if (consoleBadge == QStringLiteral("PS1"))
    return QColor(100, 181, 246);
  return QColor(38, 198, 218);
}
} // namespace

class GameTile final : public QFrame {
public:
  explicit GameTile(const LibraryGame &g, QWidget *parent = nullptr)
      : QFrame(parent), game(g) {
    setObjectName("aioGameLibraryTile");
    setProperty("aio_selected", false);
    setFixedSize(246, 214);
  }

  LibraryGame game;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());

    const bool selected = property("aio_selected").toBool();
    QPainterPath cardPath;
    cardPath.addRoundedRect(rect(), 12, 12);

    p.fillPath(cardPath,
               selected ? QColor(40, 40, 52, 255) : QColor(30, 30, 40, 255));

    const QColor stroke =
        selected ? QColor(255, 255, 255, 236) : QColor(255, 255, 255, 34);
    p.setPen(QPen(stroke, selected ? 3.0 : 1.0));
    p.drawPath(cardPath);

    const QColor badgeColor = badgeColorFor(game.consoleBadge);
    p.setPen(Qt::NoPen);
    QPainterPath badgePath;
    badgePath.addRoundedRect(QRectF(10, 10, 62, 20), 10, 10);
    p.fillPath(badgePath, QColor(badgeColor.red(), badgeColor.green(),
                                 badgeColor.blue(), 48));

    QFont badgeFont = p.font();
    badgeFont.setPixelSize(12);
    badgeFont.setWeight(QFont::DemiBold);
    p.setFont(badgeFont);
    p.setPen(badgeColor);
    p.drawText(QRectF(10, 10, 62, 20), Qt::AlignCenter, game.consoleBadge);

    QLinearGradient hero(0, 28, 0, height() - 52);
    hero.setColorAt(0.0, QColor(255, 255, 255, 14));
    hero.setColorAt(1.0, QColor(0, 0, 0, 26));
    QPainterPath heroPath;
    heroPath.addRoundedRect(QRectF(10, 34, width() - 20, height() - 84), 10,
                            10);
    p.fillPath(heroPath, hero);

    QFont initialFont = p.font();
    initialFont.setPixelSize(48);
    initialFont.setWeight(QFont::Bold);
    p.setFont(initialFont);
    const QString initial =
        game.title.isEmpty() ? QStringLiteral("?") : game.title.left(1);
    QRect initialRect(10, 34, width() - 20, height() - 84);
    p.setPen(QColor(0, 0, 0, 40));
    p.drawText(initialRect.translated(1, 1), Qt::AlignCenter,
               initial.toUpper());
    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(initialRect, Qt::AlignCenter, initial.toUpper());

    QFont titleFont = p.font();
    titleFont.setPixelSize(12);
    titleFont.setWeight(QFont::DemiBold);
    p.setFont(titleFont);
    p.setPen(QColor(228, 228, 228, 220));
    p.drawText(QRect(10, height() - 44, width() - 20, 34),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, game.title);

    if (game.unsupported) {
      // Keep the game art readable and place unsupported as a semantic badge.
      p.fillPath(cardPath, QColor(0, 0, 0, 74));
      const QRectF badgeRect(width() - 102.0, 10.0, 92.0, 22.0);
      QPainterPath badgePath;
      badgePath.addRoundedRect(badgeRect, 11, 11);
      p.fillPath(badgePath, QColor(255, 140, 70, 220));
      QFont unsupportedFont = p.font();
      unsupportedFont.setPixelSize(12);
      unsupportedFont.setWeight(QFont::Bold);
      p.setFont(unsupportedFont);
      p.setPen(QColor(24, 24, 24, 220));
      p.drawText(badgeRect, Qt::AlignCenter, QStringLiteral("Unsupported"));
    }
  }
};

GamesLibraryPage::GamesLibraryPage(QWidget *parent) : QWidget(parent) {
  setupUi();
  refresh();
}

void GamesLibraryPage::setupUi() {
  setObjectName("aioGamesLibraryPage");
  setFocusPolicy(Qt::StrongFocus);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto *header = new QWidget(this);
  header->setObjectName("aioGamesLibraryHeader");
  auto *headerLayout = new QVBoxLayout(header);
  headerLayout->setContentsMargins(32, 16, 32, 8);
  headerLayout->setSpacing(2);

  auto *titleLabel = new QLabel("Games Library", header);
  titleLabel->setObjectName("aioGamesLibraryTitle");
  headerLayout->addWidget(titleLabel);

  subtitleLabel_ = new QLabel(header);
  subtitleLabel_->setObjectName("aioGamesLibrarySubtitle");
  headerLayout->addWidget(subtitleLabel_);
  root->addWidget(header);

  filterBar_ = new QWidget(this);
  filterBar_->setObjectName("aioGamesFilterBar");
  auto *chipRow = new QHBoxLayout(filterBar_);
  chipRow->setContentsMargins(32, 0, 32, 12);
  chipRow->setSpacing(8);

  const QVector<QString> labels = {"All", "GBA", "PS1", "Switch"};
  for (const auto &labelText : labels) {
    auto *chip = new QLabel(labelText, filterBar_);
    chip->setObjectName("aioGamesFilterChip");
    chip->setAlignment(Qt::AlignCenter);
    chips_.append(chip);
    chipRow->addWidget(chip);
  }
  chipRow->addStretch();
  root->addWidget(filterBar_);

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setObjectName("aioGamesScrollArea");

  gridHost_ = new QWidget(scroll);
  gridHost_->setObjectName("aioGamesGrid");
  scroll->setWidget(gridHost_);

  emptyLabel_ = new QLabel("No games found. Add ROMs in Settings.", gridHost_);
  emptyLabel_->setObjectName("aioGamesEmptyLabel");
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->hide();

  root->addWidget(scroll, 1);
  updateFilterChips();
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

  QSettings settings("AIOServer", "GBAEmulator");
  const QString romDir =
      settings.value("romDirectory", QDir::homePath()).toString();
  const QDir dir(romDir);
  if (!dir.exists())
    return;

  const QStringList filters = {"*.gba", "*.bin", "*.cue", "*.iso", "*.img",
                               "*.xci", "*.nsp", "*.nso", "*.nro"};
  QDirIterator it(romDir, filters, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const QString ext = fi.suffix().toLower();

    LibraryGame game;
    game.path = fi.absoluteFilePath();
    game.title = fi.completeBaseName();

    if (ext == "gba") {
      game.consoleBadge = "GBA";
    } else if (ext == "bin" || ext == "cue" || ext == "iso" || ext == "img") {
      game.consoleBadge = "PS1";
    } else if (ext == "xci" || ext == "nsp" || ext == "nso" || ext == "nro") {
      game.consoleBadge = "Switch";
      game.unsupported = true;
    } else {
      continue;
    }

    allGames_.append(game);
  }

  std::sort(allGames_.begin(), allGames_.end(),
            [](const LibraryGame &a, const LibraryGame &b) {
              return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
}

void GamesLibraryPage::rebuildGrid() {
  displayGames_.clear();
  gameTiles_.clear();

  auto matchesFilter = [this](const LibraryGame &g) {
    switch (filter_) {
    case FilterMode::All:
      return true;
    case FilterMode::GBA:
      return g.consoleBadge == "GBA";
    case FilterMode::PS1:
      return g.consoleBadge == "PS1";
    case FilterMode::Switch:
      return g.consoleBadge == "Switch";
    }
    return true;
  };

  for (const auto &game : allGames_) {
    if (matchesFilter(game))
      displayGames_.append(game);
  }

  delete gridHost_->layout();
  auto *grid = new QGridLayout(gridHost_);
  grid->setContentsMargins(32, 8, 32, 24);
  grid->setHorizontalSpacing(12);
  grid->setVerticalSpacing(12);

  for (int i = 0; i < displayGames_.size(); ++i) {
    auto *tile = new GameTile(displayGames_[i], gridHost_);
    grid->addWidget(tile, i / kCols, i % kCols);
    gameTiles_.append(tile);
  }

  if (displayGames_.isEmpty()) {
    grid->addWidget(emptyLabel_, 0, 0, 1, kCols, Qt::AlignCenter);
    emptyLabel_->show();
  } else {
    emptyLabel_->hide();
  }

  subtitleLabel_->setText(QString("%1 games").arg(displayGames_.size()));
  gridRow_ = 0;
  gridCol_ = 0;
  inChips_ = displayGames_.isEmpty();
  updateFilterChips();
  updateGridFocus();
}

void GamesLibraryPage::updateFilterChips() {
  const int active = static_cast<int>(filter_);
  for (int i = 0; i < chips_.size(); ++i) {
    chips_[i]->setProperty("active", i == active ? "true" : "false");
    chips_[i]->setProperty("focused",
                           (inChips_ && i == chipFocus_) ? "true" : "false");
    chips_[i]->style()->unpolish(chips_[i]);
    chips_[i]->style()->polish(chips_[i]);
  }
}

void GamesLibraryPage::updateGridFocus() {
  for (int i = 0; i < gameTiles_.size(); ++i) {
    auto *tile = gameTiles_[i];
    const int row = i / kCols;
    const int col = i % kCols;
    const bool selected = !inChips_ && (row == gridRow_) && (col == gridCol_);
    tile->setProperty("aio_selected", selected);
    tile->style()->unpolish(tile);
    tile->style()->polish(tile);

    // Elevation shadow — focused tile lifts off the surface (tvOS/PS5 style)
    auto *shadow =
        qobject_cast<QGraphicsDropShadowEffect *>(tile->graphicsEffect());
    if (selected) {
      if (!shadow) {
        shadow = new QGraphicsDropShadowEffect(tile);
        tile->setGraphicsEffect(shadow);
      }
      shadow->setBlurRadius(32.0);
      shadow->setOffset(0.0, 10.0);
      shadow->setColor(QColor(0, 0, 0, 150));
      tile->raise();
    } else {
      if (shadow)
        tile->setGraphicsEffect(nullptr);
    }

    tile->update();
  }
}

void GamesLibraryPage::activateFocused() {
  const int idx = gridRow_ * kCols + gridCol_;
  if (idx < 0 || idx >= displayGames_.size())
    return;
  const auto &game = displayGames_[idx];
  if (game.unsupported)
    return;
  emit gameSelected(game.path);
}

void GamesLibraryPage::keyPressEvent(QKeyEvent *event) {
  const int key = event->key();
  const int size = displayGames_.size();
  const int lastIndex = qMax(0, size - 1);

  if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
    emit backRequested();
    return;
  }

  if (inChips_) {
    if (key == Qt::Key_Left) {
      chipFocus_ = qMax(0, chipFocus_ - 1);
      updateFilterChips();
      return;
    }
    if (key == Qt::Key_Right) {
      chipFocus_ = qMin(chips_.size() - 1, chipFocus_ + 1);
      updateFilterChips();
      return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      filter_ = static_cast<FilterMode>(chipFocus_);
      rebuildGrid();
      return;
    }
    if (key == Qt::Key_Down && size > 0) {
      inChips_ = false;
      updateFilterChips();
      updateGridFocus();
      return;
    }
    QWidget::keyPressEvent(event);
    return;
  }

  if (size == 0) {
    inChips_ = true;
    updateFilterChips();
    return;
  }

  if (key == Qt::Key_Left) {
    if (gridCol_ > 0)
      --gridCol_;
    updateGridFocus();
    return;
  }

  if (key == Qt::Key_Right) {
    const int maxCol =
        (gridRow_ == lastIndex / kCols) ? (lastIndex % kCols) : (kCols - 1);
    if (gridCol_ < maxCol)
      ++gridCol_;
    updateGridFocus();
    return;
  }

  if (key == Qt::Key_Up) {
    if (gridRow_ == 0) {
      inChips_ = true;
      chipFocus_ = static_cast<int>(filter_);
      updateFilterChips();
    } else {
      --gridRow_;
      const int maxCol =
          (gridRow_ == lastIndex / kCols) ? (lastIndex % kCols) : (kCols - 1);
      gridCol_ = qMin(gridCol_, maxCol);
      updateGridFocus();
    }
    return;
  }

  if (key == Qt::Key_Down) {
    const int maxRow = lastIndex / kCols;
    if (gridRow_ < maxRow) {
      ++gridRow_;
      const int maxCol =
          (gridRow_ == maxRow) ? (lastIndex % kCols) : (kCols - 1);
      gridCol_ = qMin(gridCol_, maxCol);
      updateGridFocus();
    }
    return;
  }

  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    activateFocused();
    return;
  }

  QWidget::keyPressEvent(event);
}

} // namespace AIO::GUI
