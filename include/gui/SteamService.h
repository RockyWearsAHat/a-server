#pragma once
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

namespace AIO::GUI {

struct SteamGame {
  int appId = 0;
  QString name;
  QString category;
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
  void refreshInstalledGames();
  bool isSteamInstalled() const;
  bool isInstalled(int appId) const;

signals:
  void gamesReady(const QList<AIO::GUI::SteamGame> &games);
  void fetchError(const QString &message);

private:
  QNetworkAccessManager *nam_;
  QSet<int> installedAppIds_;
  QList<SteamGame> cachedGames_;

  bool loadFromCache(QList<SteamGame> &games);
  void saveToCache(const QList<SteamGame> &games);
  void enrichWithGenres(QList<SteamGame> &games);
};

} // namespace AIO::GUI
