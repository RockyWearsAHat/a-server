#pragma once

#include <QFrame>
#include <QWidget>

class QGraphicsDropShadowEffect;
class QGridLayout;
class QLabel;
class QPropertyAnimation;

namespace AIO {
namespace GUI {

class RemoteControlServer;

enum class StreamingApp { YouTube, Netflix, DisneyPlus, Hulu, Store };

// Custom painted tile showing a brand gradient + logo glyph + service name.
class StreamingTile final : public QFrame {
  Q_OBJECT

public:
  StreamingTile(StreamingApp app, const QString &name,
                QWidget *parent = nullptr);

  StreamingApp app() const { return app_; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;

signals:
  void clicked();

private:
  void paintYouTube(QPainter &p, const QRectF &logoBox);
  void paintNetflix(QPainter &p, const QRectF &logoBox);
  void paintDisneyPlus(QPainter &p, const QRectF &logoBox);
  void paintHulu(QPainter &p, const QRectF &logoBox);

  StreamingApp app_;
  QString name_;
};

class StreamingHubWidget final : public QWidget {
  Q_OBJECT

public:
  explicit StreamingHubWidget(QWidget *parent = nullptr);

  void noteAppLaunched(AIO::GUI::StreamingApp app);
  void refreshState();

signals:
  void launchRequested(AIO::GUI::StreamingApp app);
  void homeRequested();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void loadRememberedState();
  void updateDetails();
  void setupUi();
  void updateFocus();
  void animateTile(StreamingTile *tile, bool selected);

  QLabel *title_ = nullptr;
  QLabel *headline_ = nullptr;
  QLabel *description_ = nullptr;
  QLabel *resumeLabel_ = nullptr;
  QLabel *hintLabel_ = nullptr;
  QGridLayout *tileGrid_ = nullptr;
  StreamingTile *tiles_[4]{};
  QGraphicsDropShadowEffect *shadows_[4]{};
  int focusedIndex_ = 0;
  int lastLaunchedIndex_ = -1;

  friend class AIO::GUI::RemoteControlServer;
};

} // namespace GUI
} // namespace AIO
