#pragma once
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace AIO::GUI {

struct SteamGame {
  int appId = 0;
  QString name;
  QString category;
  QString coverArtUrl;
  QStringList coverArtUrls;
  bool isInstalled = false;
  int priceUsdCents = 0;
  int discountPercent = 0;
  bool isOnSale = false;
};

class SteamService : public QObject {
  Q_OBJECT
public:
  explicit SteamService(QObject *parent = nullptr);

  void fetchTopGames();
  void fetchCatalogPage(const QString &category, const QString &query,
                        int start, int count);
  void refreshInstalledGames();
  bool isSteamInstalled() const;
  bool isInstalled(int appId) const;

  // Steam account — set Steam ID via OpenID (no API key needed)
  void setSteamId(const QString &steamId64);
  bool hasSteamId() const;

  // Steam account credentials (stored in QSettings)
  void setApiCredentials(const QString &apiKey, const QString &steamId64);
  bool hasApiCredentials() const;
  QString apiKey() const;
  QString steamId64() const;
  void clearApiCredentials();

  // Fetch the user's full owned library via the community XML feed (no API key)
  void fetchOwnedLibraryXml();
  // Fetch the user's full owned library from the Steam Web API (requires key)
  void fetchOwnedLibrary();

  // True if appId is in the last-fetched owned set
  bool isOwned(int appId) const;

signals:
  void gamesReady(const QList<AIO::GUI::SteamGame> &games);
  void gamesPageReady(const QList<AIO::GUI::SteamGame> &games, int start,
                      int totalCount, bool hasMore, const QString &category,
                      const QString &query);
  void fetchError(const QString &message);
  void ownedLibraryReady(const QSet<int> &ownedAppIds,
                         const QList<AIO::GUI::SteamGame> &games);
  void authError(const QString &message);

private:
  QNetworkAccessManager *nam_;
  QSet<int> installedAppIds_;
  QSet<int> ownedAppIds_;
  QList<SteamGame> cachedGames_;

  QString apiKey_;
  QString steamId64_;

  bool loadFromCache(QList<SteamGame> &games);
  void saveToCache(const QList<SteamGame> &games);
  void enrichWithGenres(QList<SteamGame> &games);
  QString buildCatalogUrl(const QString &category, const QString &query,
                          int start, int count) const;
};

} // namespace AIO::GUI
