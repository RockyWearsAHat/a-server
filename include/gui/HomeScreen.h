#pragma once

#include "gui/StreamingHubWidget.h" // StreamingApp enum

#include <QFrame>
#include <QScrollArea>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;

namespace AIO {
namespace GUI {

// Tile types for the unified homescreen grid.
enum class HomeTileKind {
  GBA,
  PS1,
  Switch,
  MediaServer,
  Settings,
  YouTube,
  Netflix,
  DisneyPlus,
  Hulu,
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

// Unified homescreen: scrollable grid of tiles, no chrome.
class HomeScreen final : public QWidget {
  Q_OBJECT

public:
  explicit HomeScreen(QWidget *parent = nullptr);

signals:
  void gbaRequested();
  void ps1Requested();
  void switchRequested();
  void nasRequested();
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
  void updateFocus();
  void ensureFocusVisible();
  void activateFocusedTile();
  int colsInRow(int row) const;

  QVector<QVector<HomeTile *>> rows_;
  QScrollArea *scrollArea_ = nullptr;
  QWidget *scrollContent_ = nullptr;

  int focusRow_ = 0;
  int focusCol_ = 0;
};

} // namespace GUI
} // namespace AIO
