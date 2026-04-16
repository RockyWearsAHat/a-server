#pragma once

#include "gui/SteamService.h"

#include <QColor>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
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
class QWidget;

namespace AIO::GUI {

class RemoteControlServer;

struct StoreGame {
  QString id;
  QString title;
  QString publisher;
  QString category;
  QString coverArtUrl;
  QStringList coverArtUrls;
  QString sourceLabel;
  QString description;
  int year = 0;
  double rating = 0.0;
  QColor coverColor;
  bool isInstalled = false;
  bool isOwned = false;
  bool isRomGame = false;
  int priceUsdCents = 0;
  int discountPercent = 0;
  bool isOnSale = false;
  bool hideCommerce = false;
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
  void onSteamGamesPageReady(const QList<AIO::GUI::SteamGame> &games, int start,
                             int totalCount, bool hasMore,
                             const QString &category, const QString &query);
  void maybeLoadMoreCatalog();
  void onOwnedLibraryReady(const QSet<int> &ownedAppIds,
                           const QList<AIO::GUI::SteamGame> &games);
  void onSteamAuthError(const QString &message);

private:
  void loadCatalog();
  void setupUi();
  void rebuildGrid();
  void resetGridViewport();
  void updateDetailOverlayLayout();
  void updateTabFocus();
  void updateGridFocus();
  void activateFocusedGame();
  void showDetailPanel(const StoreGame &game);
  void clearDetailPanel();
  void showComingSoonToast();
  void showSteamError(const QString &msg);
  void scanLibrary();
  void requestCatalogIfNeeded();
  void requestCatalogPage(bool reset);
  void applyActiveCategoryFilter();
  void updateShelfHeader();
  void updateLibraryFilterFocus();
  void updateSearchControls();
  bool matchesSearch(const StoreGame &game) const;
  bool matchesLibraryFilter(const StoreGame &game) const;
  bool handleSearchKey(QKeyEvent *event);
  QString activeCatalogCategoryKey() const;
  int colsInGrid() const;
  int rowsInGrid() const;
  int computeGridCols() const;

  // Steam account page
  void buildAccountPage();
  void hideAccountPage();
  void openSteamAuthDialog();
  void handleSignInKey(QKeyEvent *event);
  void updateAccountStatus();

  enum class FocusArea { Tabs, LibraryFilters, Grid, Detail, SignIn };
  enum class LibraryFilter { All, Steam, Local, Unsupported };
  FocusArea focusArea_ = FocusArea::Grid;
  LibraryFilter libraryFilter_ = LibraryFilter::All;
  int tabFocus_ = 0;
  int libraryFilterFocus_ = 0;
  int gridFocusRow_ = 0;
  int gridFocusCol_ = 0;
  QString searchQuery_;

  QStringList categories_;
  QVector<StoreGame> allGames_;
  QVector<StoreGame> filteredGames_;
  int activeCategoryIndex_ = 0;

  QWidget *headerBar_ = nullptr;
  QWidget *tabBar_ = nullptr;
  QVector<QLabel *> tabLabels_;
  QWidget *shelfHeader_ = nullptr;
  QLabel *shelfEyebrow_ = nullptr;
  QLabel *shelfTitle_ = nullptr;
  QLabel *shelfSummary_ = nullptr;
  QWidget *controlsBar_ = nullptr;
  QWidget *searchPanel_ = nullptr;
  QLabel *searchLabel_ = nullptr;
  QLabel *searchValue_ = nullptr;
  QWidget *libraryFilterBar_ = nullptr;
  QVector<QLabel *> libraryFilterLabels_;

  QWidget *contentArea_ = nullptr;
  QScrollArea *gridScroll_ = nullptr;
  QWidget *gridHost_ = nullptr;
  QVector<GameCard *> cards_;

  QFrame *detailPanel_ = nullptr;
  QWidget *detailOverlay_ = nullptr;
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
  QSet<int> ownedAppIds_;
  QList<SteamGame> ownedSteamGames_;
  bool ownedLibraryFetched_ = false;

  // Header account-status label
  QLabel *accountStatusLabel_ = nullptr;

  // Account page
  QWidget *accountPage_ = nullptr;
  QLabel *signinStatusLabel_ = nullptr;
  int signinFocus_ = 0;

  bool libraryModeActive_ = false;
  bool catalogRequested_ = false;
  bool catalogLoading_ = false;
  bool catalogHasMore_ = false;
  bool catalogRequestInFlight_ = false;
  QVector<StoreGame> libraryGames_;
  QString errorMessage_;
  QString catalogCategoryKey_ = QStringLiteral("all");
  QString catalogQuery_;
  int catalogStart_ = 0;
  int catalogTotalCount_ = 0;

  int kGridCols = 4;

  friend class AIO::GUI::RemoteControlServer;
};

} // namespace AIO::GUI
