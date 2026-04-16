#pragma once
#include <QListWidget>
#include <QString>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QPushButton;
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
  int filterIndex() const { return filterIndex_; }

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
  void updateHeroPanel();
  void activateFocused();
  void openDetailsDialog();
  int selectedIndex() const;
  int colCount() const { return kCols; }

  enum class FocusArea { Filters, Titles, Actions };
  int filterIndex_ = 0;  ///< 0 = All; 1..N = index+1 into emulatorFormats()
  FocusArea focusArea_ = FocusArea::Titles;
  int chipFocus_ = 0;
  bool inChips_ = false;
  int gridRow_ = 0;
  int gridCol_ = 0;
  int actionFocus_ = 0;

  QVector<LibraryGame> allGames_;
  QVector<LibraryGame> displayGames_;

  QLabel *subtitleLabel_ = nullptr;
  QWidget *filterBar_ = nullptr;
  QVector<QLabel *> chips_;
  QListWidget *titleList_ = nullptr;
  QLabel *heroArt_ = nullptr;
  QLabel *heroBadge_ = nullptr;
  QLabel *heroTitle_ = nullptr;
  QLabel *heroSubtitle_ = nullptr;
  QLabel *heroDescription_ = nullptr;
  QLabel *platformValueLabel_ = nullptr;
  QLabel *statusValueLabel_ = nullptr;
  QLabel *formatValueLabel_ = nullptr;
  QPushButton *launchButton_ = nullptr;
  QPushButton *infoButton_ = nullptr;
  QLabel *emptyLabel_ = nullptr;

  static constexpr int kCols = 1;

  friend class AIO::GUI::RemoteControlServer;
};

} // namespace AIO::GUI
