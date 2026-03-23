#pragma once
#include <QString>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QLabel;

namespace AIO::GUI {

class RemoteControlServer;

struct LibraryGame {
  QString path;
  QString title;
  QString consoleBadge;
  bool unsupported = false;
};

class GamesLibraryPage final : public QWidget {
  Q_OBJECT
public:
  explicit GamesLibraryPage(QWidget *parent = nullptr);
  void refresh();

signals:
  void gameSelected(const QString &path);
  void backRequested();

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void setupUi();
  void scanROMs();
  void rebuildGrid();
  void updateFilterChips();
  void updateGridFocus();
  void activateFocused();
  int colCount() const { return kCols; }

  enum class FilterMode { All, GBA, PS1, Switch };
  FilterMode filter_ = FilterMode::All;
  int chipFocus_ = 0;
  bool inChips_ = false;
  int gridRow_ = 0;
  int gridCol_ = 0;

  QVector<LibraryGame> allGames_;
  QVector<LibraryGame> displayGames_;

  QLabel *subtitleLabel_ = nullptr;
  QWidget *filterBar_ = nullptr;
  QVector<QLabel *> chips_;
  QWidget *gridHost_ = nullptr;
  QVector<QWidget *> gameTiles_;
  QLabel *emptyLabel_ = nullptr;

  static constexpr int kCols = 2;

  friend class AIO::GUI::RemoteControlServer;
};

} // namespace AIO::GUI
