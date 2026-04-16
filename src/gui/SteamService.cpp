#include "gui/SteamService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace AIO {
namespace GUI {

// Read PORT from the server .env file using the same candidate dirs that
// MainWindow::ResolveLocalServerWorkDir searches, so the two stay in sync.
static int resolveLocalServerPort() {
  const QStringList candidates = {
      QDir(QDir::currentPath()).filePath(QStringLiteral("server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../../AIO Server/server")),
  };
  for (const QString &dir : candidates) {
    const QString envPath = QDir(dir).filePath(QStringLiteral(".env"));
    QFile f(envPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
      const QString line = ts.readLine().trimmed();
      if (line.startsWith(QStringLiteral("PORT="))) {
        bool ok = false;
        const int p = line.mid(5).trimmed().toInt(&ok);
        if (ok && p > 0 && p <= 65535)
          return p;
      }
    }
  }
  return 8000; // matches server/.env default
}

// The Steam app list is fetched via the local auth server proxy to avoid
// Qt SSL issues on macOS. Falls back to a configurable URL.
static QString steamProxyUrl() {
  const QByteArray env = qgetenv("AIO_SERVER_URL");
  const QString base = env.isEmpty()
                           ? QStringLiteral("http://127.0.0.1:") +
                                 QString::number(resolveLocalServerPort())
                           : QString::fromUtf8(env);
  return base + QStringLiteral("/api/steam/apps");
}

static void appendUniqueCoverUrl(QStringList &urls, const QString &candidate) {
  if (candidate.isEmpty() || urls.contains(candidate))
    return;
  urls.append(candidate);
}

static QStringList guessedSteamCoverArtUrls(int appId) {
  const QString appBase =
      QStringLiteral("https://shared.fastly.steamstatic.com/store_item_assets/"
                     "steam/apps/%1/")
          .arg(appId);
  const QString subBase =
      QStringLiteral("https://shared.fastly.steamstatic.com/store_item_assets/"
                     "steam/subs/%1/")
          .arg(appId);
  return {appBase + QStringLiteral("library_600x900_2x.jpg"),
          appBase + QStringLiteral("library_600x900.jpg"),
          appBase + QStringLiteral("header.jpg"),
          appBase + QStringLiteral("capsule_616x353.jpg"),
          appBase + QStringLiteral("capsule_231x87.jpg"),
          subBase + QStringLiteral("header.jpg"),
          subBase + QStringLiteral("capsule_616x353.jpg"),
          subBase + QStringLiteral("capsule_231x87.jpg")};
}

static QStringList steamRootCandidates() {
  const QString home =
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  QStringList roots;

#if defined(Q_OS_MAC)
  roots << (home + "/Library/Application Support/Steam");
#elif defined(Q_OS_WIN)
  const QString pf86 = qEnvironmentVariable("PROGRAMFILES(X86)");
  const QString pf = qEnvironmentVariable("PROGRAMFILES");
  if (!pf86.isEmpty())
    roots << (pf86 + "/Steam");
  if (!pf.isEmpty())
    roots << (pf + "/Steam");
  roots << (home + "/AppData/Local/Steam");
  roots << (home + "/AppData/Roaming/Steam");
#else
  roots << (home + "/.steam/steam");
  roots << (home + "/.local/share/Steam");
  roots << (home + "/.steam/root");
  roots << (home + "/.var/app/com.valvesoftware.Steam/data/Steam");
#endif

  roots.removeAll(QString());
  return roots;
}

static QStringList steamAppsCandidates() {
  QStringList apps;
  for (const QString &root : steamRootCandidates()) {
    const QString candidate = QDir(root).filePath(QStringLiteral("steamapps"));
    if (QFileInfo::exists(candidate))
      apps << candidate;
  }
  return apps;
}

static constexpr int kMaxApps = 500;
static constexpr int kDefaultPageSize = 48;
static constexpr const char *kCacheTimestampKey = "SteamService/lastFetchTs";
static constexpr const char *kCacheSchemaVersionKey =
    "SteamService/cacheSchemaVersion";
static constexpr int kCacheSchemaVersion = 6;
static const qint64 kCacheTtlSecs = 86400;

SteamService::SteamService(QObject *parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {
  QSettings settings;
  apiKey_ = settings.value("steam/apiKey").toString();
  steamId64_ = settings.value("steam/steamId64").toString();
}

QString SteamService::buildCatalogUrl(const QString &category,
                                      const QString &query, int start,
                                      int count) const {
  QUrl url(steamProxyUrl());
  QUrlQuery urlQuery;
  urlQuery.addQueryItem(QStringLiteral("category"),
                        category.trimmed().isEmpty()
                            ? QStringLiteral("all")
                            : category.trimmed().toLower());
  urlQuery.addQueryItem(QStringLiteral("q"), query.trimmed());
  urlQuery.addQueryItem(QStringLiteral("start"),
                        QString::number(qMax(0, start)));
  urlQuery.addQueryItem(
      QStringLiteral("count"),
      QString::number(qBound(1, count <= 0 ? kDefaultPageSize : count, 100)));
  url.setQuery(urlQuery);
  return url.toString();
}

bool SteamService::isSteamInstalled() const {
  for (const QString &root : steamRootCandidates()) {
    if (QDir(root).exists())
      return true;
  }
  return false;
}

bool SteamService::isInstalled(int appId) const {
  return installedAppIds_.contains(appId);
}

void SteamService::refreshInstalledGames() {
  installedAppIds_.clear();
  const QRegularExpression re("appmanifest_(\\d+)\\.acf");

  for (const QString &steamappsPath : steamAppsCandidates()) {
    QDir dir(steamappsPath);
    const QStringList files =
        dir.entryList(QStringList("appmanifest_*.acf"), QDir::Files);
    for (const QString &filename : files) {
      QRegularExpressionMatch m = re.match(filename);
      if (m.hasMatch()) {
        installedAppIds_.insert(m.captured(1).toInt());
      }
    }
  }
}

void SteamService::enrichWithGenres(QList<SteamGame> &games) {
  QFile f(":/store/steam-genres.json");
  if (!f.open(QIODevice::ReadOnly))
    return;
  const QJsonObject map = QJsonDocument::fromJson(f.readAll()).object();
  for (SteamGame &game : games) {
    const QString key = QString::number(game.appId);
    const QJsonValue val = map.value(key);
    if (!val.isUndefined() && !val.isNull()) {
      const QString genre = val.toString();
      if (!genre.isEmpty())
        game.category = genre;
    }
  }
}

bool SteamService::loadFromCache(QList<SteamGame> &games) {
  QSettings settings;
  const int schemaVersion = settings.value(kCacheSchemaVersionKey, 0).toInt();
  if (schemaVersion != kCacheSchemaVersion)
    return false;
  const qint64 ts = settings.value(kCacheTimestampKey, 0).toLongLong();
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  if (now - ts > kCacheTtlSecs)
    return false;

  const QString cacheFile =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/steam_catalog.json";
  QFile f(cacheFile);
  if (!f.open(QIODevice::ReadOnly))
    return false;

  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isArray())
    return false;

  games.clear();
  for (const QJsonValue &val : doc.array()) {
    const QJsonObject obj = val.toObject();
    SteamGame g;
    g.appId = obj.value("appId").toInt();
    g.name = obj.value("name").toString();
    g.category = obj.value("category").toString("Other");
    g.coverArtUrl = obj.value("coverArtUrl").toString();
    const QJsonArray coverArtUrls = obj.value("coverArtUrls").toArray();
    for (const QJsonValue &coverVal : coverArtUrls) {
      const QString coverUrl = coverVal.toString();
      if (!coverUrl.isEmpty())
        g.coverArtUrls.append(coverUrl);
    }
    if (g.coverArtUrls.isEmpty() && !g.coverArtUrl.isEmpty())
      g.coverArtUrls.append(g.coverArtUrl);
    g.priceUsdCents = obj.value("priceUsdCents").toInt(0);
    g.discountPercent = obj.value("discountPercent").toInt(0);
    g.isOnSale = obj.value("isOnSale").toBool(false);
    g.isInstalled = installedAppIds_.contains(g.appId);
    games.append(g);
  }
  return !games.isEmpty();
}

void SteamService::saveToCache(const QList<SteamGame> &games) {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);

  QJsonArray arr;
  for (const SteamGame &g : games) {
    QJsonObject obj;
    obj["appId"] = g.appId;
    obj["name"] = g.name;
    obj["category"] = g.category;
    obj["coverArtUrl"] = g.coverArtUrl;
    QJsonArray coverArtUrls;
    for (const QString &coverUrl : g.coverArtUrls)
      coverArtUrls.append(coverUrl);
    obj["coverArtUrls"] = coverArtUrls;
    obj["priceUsdCents"] = g.priceUsdCents;
    obj["discountPercent"] = g.discountPercent;
    obj["isOnSale"] = g.isOnSale;
    arr.append(obj);
  }

  QFile f(dir + "/steam_catalog.json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
  }

  QSettings settings;
  settings.setValue(kCacheTimestampKey, QDateTime::currentSecsSinceEpoch());
  settings.setValue(kCacheSchemaVersionKey, kCacheSchemaVersion);
}

void SteamService::fetchTopGames() {
  refreshInstalledGames();

  QList<SteamGame> cached;
  if (loadFromCache(cached)) {
    // Mark installed games
    for (SteamGame &g : cached) {
      g.isInstalled = installedAppIds_.contains(g.appId);
    }
    emit gamesReady(cached);
    emit gamesPageReady(cached, 0, cached.size(), cached.size() >= kMaxApps,
                        QStringLiteral("all"), QString());
    return;
  }

  fetchCatalogPage(QStringLiteral("all"), QString(), 0, kDefaultPageSize);
}

void SteamService::fetchCatalogPage(const QString &category,
                                    const QString &query, int start,
                                    int count) {
  refreshInstalledGames();

  QNetworkRequest req{QUrl(buildCatalogUrl(category, query, start, count))};
  req.setHeader(QNetworkRequest::UserAgentHeader, "AIOServer/1.0");

  auto *reply = nam_->get(req);

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, category, query, start, count]() {
        const QByteArray data = reply->readAll();
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errorStr = reply->errorString();
        const bool hasNetworkError = reply->error() != QNetworkReply::NoError;

        reply->deleteLater();

        if (hasNetworkError) {
          qWarning() << "[Steam] Server proxy error:" << errorStr;
          emit fetchError(QString("Server unavailable: %1").arg(errorStr));
          return;
        }

        if (httpStatus != 200 && httpStatus != 0) {
          qWarning() << "[Steam] Server proxy HTTP" << httpStatus;
          const QJsonDocument errDoc = QJsonDocument::fromJson(data);
          const QString errMsg =
              errDoc.isObject()
                  ? errDoc.object().value("error").toString(errorStr)
                  : errorStr;
          emit fetchError(QString("Steam proxy error: %1").arg(errMsg));
          return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
          qWarning() << "Steam API response is not a JSON object";
          emit fetchError("Invalid Steam API response format");
          return;
        }

        const QJsonObject root = doc.object();
        const QJsonObject applist = root.value("applist").toObject();
        const QJsonArray apps = applist.value("apps").toArray();
        const QJsonObject meta = root.value("meta").toObject();
        const int totalCount = meta.value("total_count").toInt(apps.size());
        const bool hasMore =
            meta.value("has_more").toBool(start + apps.size() < totalCount);

        QList<SteamGame> games;
        games.reserve(apps.size());

        for (const QJsonValue &appValue : apps) {
          const QJsonObject obj = appValue.toObject();
          SteamGame g;
          g.appId = obj.value("appid").toInt();
          g.name = obj.value("name").toString();
          g.category = obj.value("category").toString("Other");
          g.coverArtUrl = obj.value("cover_art_url").toString();
          const QJsonArray coverArtUrls = obj.value("cover_art_urls").toArray();
          for (const QJsonValue &coverVal : coverArtUrls) {
            const QString coverUrl = coverVal.toString();
            if (!coverUrl.isEmpty())
              g.coverArtUrls.append(coverUrl);
          }
          if (g.coverArtUrls.isEmpty() && !g.coverArtUrl.isEmpty())
            g.coverArtUrls.append(g.coverArtUrl);
          for (const QString &guessedUrl : guessedSteamCoverArtUrls(g.appId))
            appendUniqueCoverUrl(g.coverArtUrls, guessedUrl);
          g.coverArtUrl = g.coverArtUrls.value(0);
          g.priceUsdCents = obj.value("final_price").toInt(0);
          g.discountPercent = obj.value("discount_percent").toInt(0);
          g.isOnSale = obj.value("discounted").toBool(false);
          g.isInstalled = installedAppIds_.contains(g.appId);

          if (!g.name.isEmpty() && g.appId > 0)
            games.append(g);
        }

        if (games.isEmpty() && totalCount > 0) {
          emit gamesPageReady(games, start, totalCount, false, category, query);
          return;
        }

        enrichWithGenres(games);
        if (start == 0 && query.trimmed().isEmpty() &&
            category.trimmed().compare(QStringLiteral("all"),
                                       Qt::CaseInsensitive) == 0) {
          saveToCache(games);
          emit gamesReady(games);
        }

        emit gamesPageReady(games, start, totalCount, hasMore, category, query);
      });
}

void SteamService::setSteamId(const QString &steamId64) {
  steamId64_ = steamId64.trimmed();
  QSettings settings;
  settings.setValue("steam/steamId64", steamId64_);
}

bool SteamService::hasSteamId() const {
  return !steamId64_.trimmed().isEmpty();
}

void SteamService::setApiCredentials(const QString &apiKey,
                                     const QString &steamId64) {
  apiKey_ = apiKey.trimmed();
  steamId64_ = steamId64.trimmed();
  QSettings settings;
  settings.setValue("steam/apiKey", apiKey_);
  settings.setValue("steam/steamId64", steamId64_);
}

bool SteamService::hasApiCredentials() const {
  return !apiKey_.trimmed().isEmpty() && !steamId64_.trimmed().isEmpty();
}

QString SteamService::apiKey() const { return apiKey_; }
QString SteamService::steamId64() const { return steamId64_; }

void SteamService::clearApiCredentials() {
  apiKey_.clear();
  steamId64_.clear();
  ownedAppIds_.clear();
  QSettings settings;
  settings.remove("steam/apiKey");
  settings.remove("steam/steamId64");
}

bool SteamService::isOwned(int appId) const {
  return ownedAppIds_.contains(appId);
}

void SteamService::fetchOwnedLibrary() {
  if (!hasApiCredentials()) {
    emit authError("Steam ID and API key are required. Open the Sign In panel "
                   "to set them.");
    return;
  }

  const int port = resolveLocalServerPort();
  QUrl url(QStringLiteral("http://127.0.0.1:%1/api/steam/library").arg(port));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("apikey"), apiKey_);
  query.addQueryItem(QStringLiteral("steamid"), steamId64_);
  url.setQuery(query);

  QNetworkRequest req{url};
  req.setHeader(QNetworkRequest::UserAgentHeader, "AIOServer/1.0");

  auto *reply = nam_->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const QByteArray data = reply->readAll();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool hasNetworkError = reply->error() != QNetworkReply::NoError;
    const QString errorStr = reply->errorString();
    reply->deleteLater();

    if (hasNetworkError) {
      emit authError(
          QStringLiteral("Network error fetching library: %1").arg(errorStr));
      return;
    }

    if (httpStatus == 401) {
      const QJsonDocument errDoc = QJsonDocument::fromJson(data);
      const QString msg =
          errDoc.isObject()
              ? errDoc.object().value("error").toString()
              : QStringLiteral(
                    "Invalid API key or private Steam profile. Enable "
                    "\"Game Details\" in your Steam Privacy Settings.");
      emit authError(msg);
      return;
    }

    if (httpStatus != 200 && httpStatus != 0) {
      const QJsonDocument errDoc = QJsonDocument::fromJson(data);
      const QString msg =
          errDoc.isObject()
              ? errDoc.object().value("error").toString(errorStr)
              : QStringLiteral("Steam API error (HTTP %1)").arg(httpStatus);
      emit authError(msg);
      return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
      emit authError("Invalid response from Steam library API.");
      return;
    }

    const QJsonArray games =
        doc.object().value("response").toObject().value("games").toArray();

    QSet<int> ownedIds;
    QList<SteamGame> ownedGames;
    ownedIds.reserve(games.size());
    ownedGames.reserve(games.size());

    refreshInstalledGames();

    for (const QJsonValue &val : games) {
      const QJsonObject obj = val.toObject();
      const int appId = obj.value("appid").toInt();
      if (appId <= 0)
        continue;

      ownedIds.insert(appId);

      SteamGame g;
      g.appId = appId;
      g.name = obj.value("name").toString();
      g.isInstalled = installedAppIds_.contains(appId);
      for (const QString &u : guessedSteamCoverArtUrls(appId))
        appendUniqueCoverUrl(g.coverArtUrls, u);
      g.coverArtUrl = g.coverArtUrls.value(0);
      ownedGames.append(g);
    }

    ownedAppIds_ = ownedIds;
    emit ownedLibraryReady(ownedIds, ownedGames);
  });
}

void SteamService::fetchOwnedLibraryXml() {
  if (!hasSteamId()) {
    emit authError("Sign in with Steam first to load your library.");
    return;
  }

  const int port = resolveLocalServerPort();
  QUrl url(QStringLiteral("http://127.0.0.1:%1/api/steam/games-xml").arg(port));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("steamid"), steamId64_);
  url.setQuery(q);

  QNetworkRequest req{url};
  req.setHeader(QNetworkRequest::UserAgentHeader, "AIOServer/1.0");

  auto *reply = nam_->get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const QByteArray data = reply->readAll();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool hasNetworkError = reply->error() != QNetworkReply::NoError;
    const QString errorStr = reply->errorString();
    reply->deleteLater();

    if (hasNetworkError) {
      emit authError(
          QStringLiteral("Network error fetching library: %1").arg(errorStr));
      return;
    }

    if (httpStatus == 403) {
      const QJsonDocument errDoc = QJsonDocument::fromJson(data);
      const QString msg =
          errDoc.isObject()
              ? errDoc.object().value("error").toString()
              : QStringLiteral(
                    "Steam profile is private. Set \"Game Details\" to "
                    "Public in Steam Privacy Settings.");
      emit authError(msg);
      return;
    }

    if (httpStatus != 200 && httpStatus != 0) {
      emit authError(
          QStringLiteral("Steam games XML error (HTTP %1)").arg(httpStatus));
      return;
    }

    // Parse the Steam community games XML feed.
    // <gamesList><games><game><appID>...</appID><name>...</name></game></games>
    // Use readNext() directly to avoid the readNextStartElement() + readNext()
    // double-advance bug where sibling elements get skipped after
    // readElementText().
    QXmlStreamReader xml(data);
    QSet<int> ownedIds;
    QList<SteamGame> ownedGames;
    bool inGame = false;
    int appId = 0;
    QString gameName;

    refreshInstalledGames();

    while (!xml.atEnd() && !xml.hasError()) {
      const auto token = xml.readNext();

      if (token == QXmlStreamReader::StartElement) {
        const QStringView name = xml.name();
        if (name == QLatin1String("game")) {
          inGame = true;
          appId = 0;
          gameName.clear();
        } else if (inGame && name == QLatin1String("appID")) {
          appId = xml.readElementText().trimmed().toInt();
        } else if (inGame && name == QLatin1String("name")) {
          gameName = xml.readElementText().trimmed();
        } else if (name == QLatin1String("error")) {
          const QString errText = xml.readElementText().trimmed();
          emit authError(
              errText.isEmpty()
                  ? QStringLiteral("Steam returned an error. Set your profile "
                                   "Game Details to Public.")
                  : errText);
          return;
        }
      } else if (token == QXmlStreamReader::EndElement) {
        if (xml.name() == QLatin1String("game") && inGame) {
          inGame = false;
          if (appId > 0) {
            ownedIds.insert(appId);
            SteamGame g;
            g.appId = appId;
            g.name = gameName;
            g.isInstalled = installedAppIds_.contains(appId);
            for (const QString &u : guessedSteamCoverArtUrls(appId))
              appendUniqueCoverUrl(g.coverArtUrls, u);
            g.coverArtUrl = g.coverArtUrls.value(0);
            ownedGames.append(g);
          }
        }
      }
    }

    if (xml.hasError()) {
      emit authError(QStringLiteral("Failed to parse Steam games feed: %1")
                         .arg(xml.errorString()));
      return;
    }

    ownedAppIds_ = ownedIds;
    emit ownedLibraryReady(ownedIds, ownedGames);
  });
}

} // namespace GUI
} // namespace AIO
