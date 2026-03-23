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
static constexpr const char *kCacheTimestampKey = "SteamService/lastFetchTs";
static constexpr const char *kCacheSchemaVersionKey =
    "SteamService/cacheSchemaVersion";
static constexpr int kCacheSchemaVersion = 2;
static const qint64 kCacheTtlSecs = 86400;

SteamService::SteamService(QObject *parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

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
    return;
  }

  // Fetch via local server proxy (plain HTTP, no SSL issues)
  QNetworkRequest req{QUrl(steamProxyUrl())};
  req.setHeader(QNetworkRequest::UserAgentHeader, "AIOServer/1.0");

  auto *reply = nam_->get(req);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
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
      // Try to extract a human-readable error from the JSON body
      const QJsonDocument errDoc = QJsonDocument::fromJson(data);
      const QString errMsg =
          errDoc.isObject() ? errDoc.object().value("error").toString(errorStr)
                            : errorStr;
      emit fetchError(QString("Steam proxy error: %1").arg(errMsg));
      return;
    }

    // Parse JSON response
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
      qWarning() << "Steam API returned invalid JSON";
      emit fetchError("Steam API returned invalid JSON");
      return;
    }

    if (!doc.isObject()) {
      qWarning() << "Steam API response is not a JSON object";
      emit fetchError("Invalid Steam API response format");
      return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject applist = root.value("applist").toObject();
    const QJsonArray apps = applist.value("apps").toArray();

    if (apps.isEmpty()) {
      qWarning() << "Steam API returned empty app list";
      emit fetchError("No games found in Steam API response");
      return;
    }

    qDebug() << "Steam API returned" << apps.size() << "games";

    QList<SteamGame> games;
    games.reserve(qMin(kMaxApps, apps.size()));

    // Map raw category keys to human-readable labels.
    static const QHash<QString, QString> kCategoryLabels = {
        {QStringLiteral("top_sellers"), QStringLiteral("Top Sellers")},
        {QStringLiteral("new_releases"), QStringLiteral("New Releases")},
        {QStringLiteral("specials"), QStringLiteral("On Sale")},
        {QStringLiteral("coming_soon"), QStringLiteral("Coming Soon")},
    };

    for (int i = 0; i < apps.size() && i < kMaxApps; ++i) {
      const QJsonObject obj = apps.at(i).toObject();
      SteamGame g;
      g.appId = obj.value("appid").toInt();
      g.name = obj.value("name").toString();
      const QString rawCat = obj.value("category").toString();
      g.category = kCategoryLabels.value(rawCat, QStringLiteral("Other"));
      g.isInstalled = installedAppIds_.contains(g.appId);
      g.priceUsdCents = obj.value("final_price").toInt(0);
      g.discountPercent = obj.value("discount_percent").toInt(0);
      g.isOnSale = obj.value("discounted").toBool(false);

      if (!g.name.isEmpty() && g.appId > 0) {
        games.append(g);
      }
    }

    if (games.isEmpty()) {
      qWarning() << "No valid games parsed from Steam API";
      emit fetchError("Failed to parse games from Steam API");
      return;
    }

    qDebug() << "Parsed" << games.size() << "games from Steam API";
    enrichWithGenres(games);
    saveToCache(games);
    emit gamesReady(games);
  });
}

} // namespace GUI
} // namespace AIO
