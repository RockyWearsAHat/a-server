#pragma once

#include "gui/SteamService.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QVBoxLayout;

namespace AIO::GUI {

class RemoteControlServer;

struct StoreGame {
  QString id;
  QString title;
  QString publisher;
  QString category;
  QString sourceLabel;
  QString description;
  int year = 0;
  double rating = 0.0;
  QColor coverColor;
  bool isInstalled = false;
  bool isRomGame = false;
  int priceUsdCents = 0;
  int discountPercent = 0;
  bool isOnSale = false;
};

class GameCard;

class GameStorePage final : public QWidget {
  Q_OBJECT

public:
  explicit GameStorePage(QWidget *parent = nullptr);
  void setSteamService(SteamService *service);

signals:
  void homeRequested();
  void gameSelected(const QString &steamAppId);
  void romLaunchRequested(const QString &path);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void onSteamGamesReady(const QList<AIO::GUI::SteamGame> &games);

private:
  void loadCatalog();
  void setupUi();
  void rebuildGrid();
  void updateTabFocus();
  void updateGridFocus();
  void activateFocusedGame();
  void showDetailPanel(const StoreGame &game);
  void clearDetailPanel();
  void showComingSoonToast();
  void showSteamError(const QString &msg);
  void scanLibrary();
  int colsInGrid() const;
  int rowsInGrid() const;
  int computeGridCols() const;

  enum class FocusArea { Tabs, Grid, Detail };
  FocusArea focusArea_ = FocusArea::Grid;
  int tabFocus_ = 0;
  int gridFocusRow_ = 0;
  int gridFocusCol_ = 0;

  QStringList categories_;
  QVector<StoreGame> allGames_;
  QVector<StoreGame> filteredGames_;
  int activeCategoryIndex_ = 0;

  QWidget *headerBar_ = nullptr;
  QWidget *tabBar_ = nullptr;
  QVector<QLabel *> tabLabels_;

  QWidget *contentArea_ = nullptr;
  QScrollArea *gridScroll_ = nullptr;
  QWidget *gridHost_ = nullptr;
  QVector<GameCard *> cards_;

  QFrame *detailPanel_ = nullptr;
  QLabel *detailArt_ = nullptr;
  QLabel *detailTitle_ = nullptr;
  QLabel *detailPublisher_ = nullptr;
  QLabel *detailYear_ = nullptr;
  QLabel *detailRating_ = nullptr;
  QLabel *detailCategory_ = nullptr;
  QLabel *detailDescription_ = nullptr;
  QPushButton *installBtn_ = nullptr;
  bool detailVisible_ = false;
  bool errorShown_ = false;

  QWidget *toastWidget_ = nullptr;
  QTimer *toastTimer_ = nullptr;

  SteamService *steamService_ = nullptr;
  QList<SteamGame> steamGames_;

  QWidget *loadingOverlay_ = nullptr;
  QLabel *loadingLabel_ = nullptr;

  bool libraryModeActive_ = false;
  QVector<StoreGame> libraryGames_;

  int kGridCols = 4;

  friend class AIO::GUI::RemoteControlServer;
};

} // namespace AIO::GUI
