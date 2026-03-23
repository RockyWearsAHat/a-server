#pragma once

#include "gui/StreamingApp.h"

#include <QFrame>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace AIO {
namespace GUI {

// Tile types for the unified homescreen grid.
enum class HomeTileKind {
  GBA,
  PS1,
  Switch,
  MediaServer,
  ScreenMirror,
  Settings,
  YouTube,
  Netflix,
  DisneyPlus,
  Hulu,
  Store,
  Library,
  Blank
};

// Flat, uniform tile — paints its own background, border, icon, and label.
class HomeTile final : public QFrame {
  Q_OBJECT
  Q_PROPERTY(qreal focusProgress READ focusProgress WRITE setFocusProgress)

public:
  HomeTile(HomeTileKind kind, QWidget *parent = nullptr);
  HomeTileKind kind() const { return kind_; }

  qreal focusProgress() const { return focusProgress_; }
  void setFocusProgress(qreal v) {
    focusProgress_ = v;
    update();
  }

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

signals:
  void clicked();

private:
  void paintIcon(QPainter &p, const QRectF &box, const QColor &col);
  HomeTileKind kind_;
  qreal focusProgress_ = 0.0;
};

// Unified homescreen: one customizable app surface for the whole system.
class HomeScreen final : public QWidget {
  Q_OBJECT

public:
  explicit HomeScreen(QWidget *parent = nullptr);
  bool organizeMode() const { return organizeMode_; }

signals:
  void gbaRequested();
  void ps1Requested();
  void switchRequested();
  void nasRequested();
  void storeRequested();
  void libraryRequested();
  void screenMirrorRequested();
  void settingsRequested();
  void streamingAppRequested(AIO::GUI::StreamingApp app);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  friend class RemoteControlServer;
  void setupUi();
  void rebuildGrid(HomeTileKind preferredKind = HomeTileKind::YouTube);
  void updateHero();
  void updateFocus();
  void ensureFocusVisible();
  void activateFocusedTile();
  void activateTileKind(HomeTileKind kind);
  void toggleOrganizeMode();
  void resetLayout();
  void moveFocusedTile(int rowDelta, int colDelta);
  void hideFocusedTile();
  void showAddAppsOverlay();
  void hideAddAppsOverlay();
  void unhideTile(HomeTileKind kind);
  void loadTileOrder();
  void saveTileOrder() const;
  int currentColumnCount() const;
  HomeTile *currentFocusedTile() const;
  HomeTile *tileForKind(HomeTileKind kind) const;
  bool findTile(HomeTileKind kind, int &row, int &col) const;
  int colsInRow(int row) const;

  QVector<HomeTileKind> tileOrder_;
  QVector<HomeTileKind> hiddenTiles_;
  QVector<QVector<HomeTile *>> rows_;
  QScrollArea *scrollArea_ = nullptr;
  QWidget *scrollContent_ = nullptr;
  QWidget *gridHost_ = nullptr;
  QGridLayout *gridLayout_ = nullptr;
  QLabel *eyebrowLabel_ = nullptr;
  QLabel *titleLabel_ = nullptr;
  QLabel *subtitleLabel_ = nullptr;
  QLabel *modeChipLabel_ = nullptr;
  QLabel *hintLabel_ = nullptr;

  // Add-apps overlay (shown in organize mode to restore hidden tiles)
  QWidget *addAppsOverlay_ = nullptr;
  QVBoxLayout *addAppsLayout_ = nullptr;
  int addAppsFocus_ = 0;

  int focusRow_ = 0;
  int focusCol_ = 0;
  int gridColumnCount_ = 0;
  bool organizeMode_ = false;
};

} // namespace GUI
} // namespace AIO
