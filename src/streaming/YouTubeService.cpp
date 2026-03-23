#include "streaming/YouTubeService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <curl/curl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace AIO {
namespace Streaming {

namespace {

constexpr auto kSettingsOrg = "AIOServer";
constexpr auto kSettingsApp = "GBAEmulator";
constexpr auto kYouTubeReadonlyScope =
    "https://www.googleapis.com/auth/youtube.readonly";
constexpr auto kDefaultYouTubeProxyUrl = "http://127.0.0.1:8916";
constexpr int kExtraDiscoveryRailsPerLevel = 2;

struct ExtraDiscoveryRailSpec {
  const char *key;
  const char *title;
  const char *subtitle;
  const char *videoCategoryId;
};

constexpr ExtraDiscoveryRailSpec kExtraDiscoveryRails[] = {
    {"science", "Science and tech",
     "Popular science and technology videos beyond the default home rows.",
     "28"},
    {"sports", "Sports highlights",
     "Top sports clips, commentary, and live-event coverage.", "17"},
    {"news_more", "News and commentary",
     "Additional public news and politics videos for deeper browsing.", "25"},
    {"learning", "How-to and learning",
     "Tutorials, explainers, and practical learning videos.", "27"},
    {"film", "Film and animation",
     "Popular trailers, animation, and film-related uploads.", "1"},
    {"travel", "Travel and events",
     "Travel videos, destination guides, and live event content.", "19"},
    {"comedy", "Comedy picks",
     "Popular comedy uploads for lighter browsing depth.", "23"},
    {"lifestyle", "How-to and style",
     "Lifestyle, design, and style content outside the default rails.", "26"},
};

struct HttpResponse {
  CURLcode curlCode = CURLE_OK;
  long statusCode = 0;
  std::string body;
};

std::string UrlEncodeQuery(const std::string &text) {
  std::ostringstream enc;
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      enc << c;
    } else if (c == ' ') {
      enc << '+';
    } else {
      enc << '%' << std::uppercase << std::hex << std::setw(2)
          << std::setfill('0') << static_cast<int>(c) << std::nouppercase
          << std::dec;
    }
  }
  return enc.str();
}

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  static_cast<std::string *>(userp)->append(static_cast<char *>(contents),
                                            size * nmemb);
  return size * nmemb;
}

HttpResponse PerformHttpRequest(const std::string &url,
                                const std::string &postFields,
                                const std::vector<std::string> &extraHeaders,
                                long timeoutSeconds) {
  HttpResponse result;

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.curlCode = CURLE_FAILED_INIT;
    return result;
  }

  struct curl_slist *headers = nullptr;
  for (const auto &header : extraHeaders) {
    headers = curl_slist_append(headers, header.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  if (!postFields.empty()) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
  }

  result.curlCode = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.statusCode);

  if (headers) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl);
  return result;
}

std::string TrimTrailingSlash(const std::string &value) {
  if (value.empty()) {
    return value;
  }

  size_t end = value.size();
  while (end > 0 && value[end - 1] == '/') {
    --end;
  }
  return value.substr(0, end);
}

bool EnvFlagEnabled(const char *name) {
  if (const char *value = std::getenv(name); value && *value) {
    return std::string(value) != "0";
  }
  return false;
}

void ClearStoredDirectYouTubeCredentials(QSettings &settings) {
  settings.remove("youtube/oauth/accessToken");
  settings.remove("youtube/oauth/refreshToken");
  settings.remove("youtube/oauth/clientId");
  settings.remove("youtube/oauth/clientSecret");
}

QString DefaultServerWorkDir() {
  const QString configured = qEnvironmentVariable("AIO_YOUTUBE_SERVER_WORKDIR");
  if (!configured.isEmpty()) {
    return QFileInfo(configured).absoluteFilePath();
  }

  const QStringList candidates = {
      QDir(QDir::currentPath()).filePath(QStringLiteral("server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../server")),
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../../../AIO Server/server")),
  };

  for (const QString &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isDir()) {
      return info.absoluteFilePath();
    }
  }

  return QDir(QDir::currentPath()).filePath(QStringLiteral("server"));
}

QString ReadServerEnvValue(const QString &key) {
  const QString envPath = QDir(DefaultServerWorkDir()).filePath(".env");
  QFile file(envPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QTextStream stream(&file);
  while (!stream.atEnd()) {
    QString line = stream.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    const int separator = line.indexOf('=');
    if (separator <= 0) {
      continue;
    }

    const QString currentKey = line.left(separator).trimmed();
    if (currentKey != key) {
      continue;
    }

    QString value = line.mid(separator + 1).trimmed();
    if ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith('\'') && value.endsWith('\''))) {
      value = value.mid(1, value.size() - 2);
    }
    return value;
  }

  return QString();
}

std::string ResolveAutobootProxyUrl() {
  const QString configuredPort = ReadServerEnvValue(QStringLiteral("PORT"));
  bool ok = false;
  const int port = configuredPort.toInt(&ok);
  if (ok && port > 0 && port <= 65535) {
    return QStringLiteral("http://127.0.0.1:%1").arg(port).toStdString();
  }
  return kDefaultYouTubeProxyUrl;
}

QString JsonString(const QJsonObject &obj, const char *key) {
  return obj.value(QString::fromUtf8(key)).toString();
}

QString JsonMessageFromResponseBody(const std::string &body) {
  if (body.empty()) {
    return {};
  }

  QJsonParseError error{};
  const auto doc =
      QJsonDocument::fromJson(QByteArray::fromStdString(body), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return {};
  }

  return doc.object().value(QStringLiteral("message")).toString();
}

bool IsInvalidProxySessionMessage(const QString &message) {
  return message == QStringLiteral("Unknown YouTube session.") ||
         message == QStringLiteral("Missing YouTube session id.");
}

QString PickBestThumb(const QJsonObject &thumbs) {
  auto pick = [&](const char *key) -> QString {
    return thumbs.value(key).toObject().value("url").toString();
  };

  QString url = pick("maxres");
  if (url.isEmpty())
    url = pick("standard");
  if (url.isEmpty())
    url = pick("high");
  if (url.isEmpty())
    url = pick("medium");
  if (url.isEmpty())
    url = pick("default");
  return url;
}

template <typename T>
void AppendUniqueVideos(std::vector<VideoContent> &target,
                        std::unordered_set<std::string> &seenIds, T &&source,
                        int limit, const std::string &categoryOverride = {}) {
  for (auto &item : source) {
    if (item.id.empty() || seenIds.contains(item.id)) {
      continue;
    }
    seenIds.insert(item.id);
    if (!categoryOverride.empty()) {
      item.category = categoryOverride;
    }
    target.push_back(std::move(item));
    if ((int)target.size() >= limit) {
      return;
    }
  }
}

} // namespace

YouTubeService::YouTubeService() : authenticated_(false) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

void YouTubeService::loadStoredStateLocked() {
  if (stateLoaded_) {
    return;
  }

  if (const char *proxyBaseUrl = std::getenv("AIO_YOUTUBE_SERVER_URL");
      proxyBaseUrl && *proxyBaseUrl) {
    proxyBaseUrl_ = TrimTrailingSlash(proxyBaseUrl);
  } else if (EnvFlagEnabled("AIO_YOUTUBE_SERVER_AUTOBOOT")) {
    proxyBaseUrl_ = ResolveAutobootProxyUrl();
  }
  if (const char *region = std::getenv("YOUTUBE_REGION"); region && *region) {
    regionCode_ = region;
  }

  QSettings settings(kSettingsOrg, kSettingsApp);
  if (proxySessionId_.empty()) {
    proxySessionId_ =
        settings.value("youtube/server/sessionId").toString().toStdString();
  }
  apiKey_.clear();
  accessToken_.clear();
  refreshToken_.clear();
  oauthClientId_.clear();
  oauthClientSecret_.clear();
  ClearStoredDirectYouTubeCredentials(settings);

  if (accountDisplayName_.empty()) {
    accountDisplayName_ =
        settings.value("youtube/oauth/accountName").toString().toStdString();
  }
  if (accountAvatarUrl_.empty()) {
    accountAvatarUrl_ = settings.value("youtube/oauth/accountAvatarUrl")
                            .toString()
                            .toStdString();
  }

  if (proxyBaseUrl_.empty() && !proxySessionId_.empty()) {
    proxyBaseUrl_ = kDefaultYouTubeProxyUrl;
  }

  const bool storedAuthenticated =
      settings.value("youtube/server/authenticated", false).toBool();
  authenticated_ = !proxySessionId_.empty() && storedAuthenticated;
  stateLoaded_ = true;
}

void YouTubeService::saveOAuthStateLocked() const {
  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.setValue("youtube/server/sessionId",
                    QString::fromStdString(proxySessionId_));
  settings.setValue("youtube/server/authenticated", authenticated_);
  ClearStoredDirectYouTubeCredentials(settings);
  settings.setValue("youtube/oauth/accountName",
                    QString::fromStdString(accountDisplayName_));
  settings.setValue("youtube/oauth/accountAvatarUrl",
                    QString::fromStdString(accountAvatarUrl_));
}

void YouTubeService::clearInvalidProxySessionLocked() {
  proxySessionId_.clear();
  accountDisplayName_.clear();
  accountAvatarUrl_.clear();
  authenticated_ = false;

  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.remove("youtube/server/sessionId");
  settings.remove("youtube/server/authenticated");
  settings.remove("youtube/oauth/accountName");
  settings.remove("youtube/oauth/accountAvatarUrl");
}

void YouTubeService::clearOAuthStateLocked() {
  clearInvalidProxySessionLocked();
  accessToken_.clear();
  refreshToken_.clear();
  pendingDeviceAuth_ = PendingDeviceAuth{};

  QSettings settings(kSettingsOrg, kSettingsApp);
  ClearStoredDirectYouTubeCredentials(settings);
}

bool YouTubeService::usingProxyServerLocked() const {
  return !proxyBaseUrl_.empty();
}

void YouTubeService::ensureAuthenticatedFromEnvironment() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadStoredStateLocked();
}

bool YouTubeService::authenticate(const StreamingCredentials &creds) {
  (void)creds;
  std::lock_guard<std::mutex> lock(mutex_);
  loadStoredStateLocked();
  authenticated_ = !proxySessionId_.empty();
  saveOAuthStateLocked();
  return authenticated_;
}

bool YouTubeService::isAuthenticated() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return authenticated_;
}

bool YouTubeService::hasOAuthAccess() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stateLoaded_) {
    const_cast<YouTubeService *>(this)->loadStoredStateLocked();
  }
  return authenticated_ && !proxySessionId_.empty();
}

bool YouTubeService::hasDeviceAuthClient() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stateLoaded_) {
    const_cast<YouTubeService *>(this)->loadStoredStateLocked();
  }
  return !proxyBaseUrl_.empty();
}

void YouTubeService::logout() {
  std::string tokenToRevoke;
  std::string proxyBaseUrl;
  std::string proxySessionId;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    proxyBaseUrl = proxyBaseUrl_;
    proxySessionId = proxySessionId_;
    tokenToRevoke = !refreshToken_.empty() ? refreshToken_ : accessToken_;
    clearOAuthStateLocked();
  }

  if (!proxyBaseUrl.empty() && !proxySessionId.empty()) {
    QJsonObject payload;
    payload.insert(QStringLiteral("sessionId"),
                   QString::fromStdString(proxySessionId));
    const auto response = PerformHttpRequest(
        proxyBaseUrl + "/api/youtube/logout",
        QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString(),
        {"Accept: application/json", "Content-Type: application/json"}, 15L);
    if (response.curlCode != CURLE_OK || response.statusCode >= 400) {
      std::cerr << "[YouTube] Proxy logout failed" << std::endl;
    }
  }

  if (!tokenToRevoke.empty()) {
    revokeTokenBestEffort(tokenToRevoke);
  }

  std::cout << "[YouTube] Logged out" << std::endl;
}

bool YouTubeService::refreshAccessTokenLocked() {
  loadStoredStateLocked();
  if (oauthClientId_.empty() || refreshToken_.empty()) {
    return false;
  }

  std::string fields = "client_id=" + UrlEncodeQuery(oauthClientId_) +
                       "&refresh_token=" + UrlEncodeQuery(refreshToken_) +
                       "&grant_type=refresh_token";
  if (!oauthClientSecret_.empty()) {
    fields += "&client_secret=" + UrlEncodeQuery(oauthClientSecret_);
  }

  const auto response =
      PerformHttpRequest("https://oauth2.googleapis.com/token", fields,
                         {"Accept: application/json",
                          "Content-Type: application/x-www-form-urlencoded"},
                         20L);
  if (response.curlCode != CURLE_OK || response.statusCode < 200 ||
      response.statusCode >= 300) {
    return false;
  }

  const auto doc =
      QJsonDocument::fromJson(QByteArray::fromStdString(response.body));
  const auto obj = doc.object();
  const QString accessToken = obj.value("access_token").toString();
  if (accessToken.isEmpty()) {
    return false;
  }

  accessToken_ = accessToken.toStdString();
  if (const QString refreshToken = obj.value("refresh_token").toString();
      !refreshToken.isEmpty()) {
    refreshToken_ = refreshToken.toStdString();
  }
  authenticated_ =
      !apiKey_.empty() || !accessToken_.empty() || !refreshToken_.empty();
  saveOAuthStateLocked();
  return true;
}

bool YouTubeService::revokeTokenBestEffort(const std::string &token) const {
  if (token.empty()) {
    return false;
  }

  const auto response = PerformHttpRequest(
      "https://oauth2.googleapis.com/revoke", "token=" + UrlEncodeQuery(token),
      {"Content-Type: application/x-www-form-urlencoded"}, 15L);
  return response.curlCode == CURLE_OK && response.statusCode == 200;
}

YouTubeDeviceAuthSession YouTubeService::beginDeviceAuth() {
  ensureAuthenticatedFromEnvironment();

  YouTubeDeviceAuthSession session;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    session.available = usingProxyServerLocked();
    session.authenticated = !proxySessionId_.empty();
    if (!usingProxyServerLocked()) {
      session.needsClientConfiguration = true;
      session.statusMessage =
          "YouTube auth server is not configured. Set "
          "AIO_YOUTUBE_SERVER_AUTOBOOT=1 or AIO_YOUTUBE_SERVER_URL.";
      return session;
    }

    const auto response = PerformHttpRequest(
        proxyBaseUrl_ + "/api/youtube/device/start", "{}",
        {"Accept: application/json", "Content-Type: application/json"}, 20L);
    const auto doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(response.body));
    const auto obj = doc.object();
    if (response.curlCode != CURLE_OK) {
      session.statusMessage =
          "Could not reach the YouTube auth server. Start the Node server or "
          "enable AIO_YOUTUBE_SERVER_AUTOBOOT.";
      return session;
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
      QString statusMessage = JsonString(obj, "message");
      const int upstreamStatus =
          obj.value(QStringLiteral("upstreamStatus")).toInt();
      if (statusMessage.isEmpty()) {
        statusMessage = QStringLiteral("YouTube auth server returned HTTP %1.")
                            .arg(response.statusCode);
      } else if (upstreamStatus > 0) {
        statusMessage +=
            QStringLiteral(" (Google HTTP %1)").arg(upstreamStatus);
      }
      session.statusMessage = statusMessage.toStdString();
      return session;
    }

    proxySessionId_ = JsonString(obj, "sessionId").toStdString();
    accountDisplayName_ = JsonString(obj, "accountDisplayName").toStdString();
    accountAvatarUrl_ = JsonString(obj, "accountAvatarUrl").toStdString();
    authenticated_ = obj.value(QStringLiteral("authenticated")).toBool();

    session.active = obj.value(QStringLiteral("active")).toBool();
    session.authenticated = authenticated_;
    session.verificationUrl = JsonString(obj, "verificationUrl").toStdString();
    session.verificationUrlComplete =
        JsonString(obj, "verificationUrlComplete").toStdString();
    session.userCode = JsonString(obj, "userCode").toStdString();
    QString statusMessage = JsonString(obj, "statusMessage");
    if (statusMessage.isEmpty()) {
      statusMessage = obj.value(QStringLiteral("message")).toString();
    }
    session.statusMessage = statusMessage.toStdString();
    session.pollIntervalSeconds =
        std::max(1, obj.value(QStringLiteral("pollIntervalSeconds")).toInt(5));
    session.expiresInSeconds =
        std::max(0, obj.value(QStringLiteral("expiresInSeconds")).toInt());
    session.secondsRemaining =
        std::max(0, obj.value(QStringLiteral("secondsRemaining")).toInt());
    saveOAuthStateLocked();
    return session;
  }
}

YouTubeDeviceAuthSession YouTubeService::pollDeviceAuth() {
  ensureAuthenticatedFromEnvironment();

  YouTubeDeviceAuthSession session;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    session.available = usingProxyServerLocked();
    session.authenticated = !proxySessionId_.empty();

    if (!usingProxyServerLocked()) {
      session.needsClientConfiguration = true;
      session.statusMessage =
          "YouTube auth server is not configured. Set "
          "AIO_YOUTUBE_SERVER_AUTOBOOT=1 or AIO_YOUTUBE_SERVER_URL.";
      return session;
    }
    if (proxySessionId_.empty()) {
      session.statusMessage =
          "No YouTube device-auth session is active. Start sign-in again.";
      return session;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("sessionId"),
                   QString::fromStdString(proxySessionId_));
    const auto response = PerformHttpRequest(
        proxyBaseUrl_ + "/api/youtube/device/poll",
        QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString(),
        {"Accept: application/json", "Content-Type: application/json"}, 20L);
    if (response.curlCode != CURLE_OK || response.statusCode < 200 ||
        response.statusCode >= 300) {
      session.statusMessage =
          "Could not reach the YouTube auth server while polling.";
      return session;
    }

    const auto doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(response.body));
    const auto obj = doc.object();
    accountDisplayName_ = JsonString(obj, "accountDisplayName").toStdString();
    accountAvatarUrl_ = JsonString(obj, "accountAvatarUrl").toStdString();
    authenticated_ = obj.value(QStringLiteral("authenticated")).toBool();
    session.active = obj.value(QStringLiteral("active")).toBool();
    session.authenticated = authenticated_;
    session.verificationUrl = JsonString(obj, "verificationUrl").toStdString();
    session.verificationUrlComplete =
        JsonString(obj, "verificationUrlComplete").toStdString();
    session.userCode = JsonString(obj, "userCode").toStdString();
    QString statusMessage = JsonString(obj, "statusMessage");
    if (statusMessage.isEmpty()) {
      statusMessage = obj.value(QStringLiteral("message")).toString();
    }
    session.statusMessage = statusMessage.toStdString();
    session.pollIntervalSeconds =
        std::max(1, obj.value(QStringLiteral("pollIntervalSeconds")).toInt(5));
    session.expiresInSeconds =
        std::max(0, obj.value(QStringLiteral("expiresInSeconds")).toInt());
    session.secondsRemaining =
        std::max(0, obj.value(QStringLiteral("secondsRemaining")).toInt());
    saveOAuthStateLocked();
    return session;
  }
}

void YouTubeService::cancelDeviceAuth() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadStoredStateLocked();
  pendingDeviceAuth_ = PendingDeviceAuth{};
}

std::string YouTubeService::getAccountDisplayName() {
  ensureAuthenticatedFromEnvironment();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    if (!authenticated_) {
      return {};
    }
    if (!accountDisplayName_.empty()) {
      return accountDisplayName_;
    }
    if (!usingProxyServerLocked() || proxySessionId_.empty()) {
      return {};
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    QUrl url(QString::fromStdString(proxyBaseUrl_ + "/api/youtube/account"));
    std::vector<std::string> headers = {"Accept: application/json"};
    if (!proxySessionId_.empty()) {
      headers.push_back("X-AIO-YouTube-Session: " + proxySessionId_);
    }
    const auto response =
        PerformHttpRequest(url.toString().toStdString(), {}, headers, 20L);
    if (response.curlCode != CURLE_OK) {
      return {};
    }
    if (response.statusCode == 401) {
      const QString message = JsonMessageFromResponseBody(response.body);
      if (IsInvalidProxySessionMessage(message)) {
        clearInvalidProxySessionLocked();
        saveOAuthStateLocked();
      }
      return {};
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
      return {};
    }

    const auto doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(response.body));
    const auto obj = doc.object();
    authenticated_ = obj.value(QStringLiteral("authenticated")).toBool();
    const std::string name =
        JsonString(obj, "accountDisplayName").toStdString();
    const std::string avatarUrl =
        JsonString(obj, "accountAvatarUrl").toStdString();
    if (!name.empty()) {
      accountDisplayName_ = name;
    }
    if (!avatarUrl.empty()) {
      accountAvatarUrl_ = avatarUrl;
    }
    saveOAuthStateLocked();
    return name;
  }
}

std::string YouTubeService::getAccountAvatarUrl() {
  ensureAuthenticatedFromEnvironment();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    if (!authenticated_) {
      return {};
    }
    if (!accountAvatarUrl_.empty()) {
      return accountAvatarUrl_;
    }
  }

  getAccountDisplayName();

  std::lock_guard<std::mutex> lock(mutex_);
  return accountAvatarUrl_;
}

std::optional<YouTubeResolvedStream>
YouTubeService::resolvePlaybackStream(const std::string & /*videoId*/) {
  // Playback now handled by the embedded YouTube web player.
  return std::nullopt;
}

std::string YouTubeService::makeApiRequest(const std::string &endpoint,
                                           const std::string &params,
                                           bool requireAuth) {
  ensureAuthenticatedFromEnvironment();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loadStoredStateLocked();
    if (usingProxyServerLocked()) {
      QUrl url(QString::fromStdString(proxyBaseUrl_ + "/api/youtube/proxy"));
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("endpoint"),
                         QString::fromStdString(endpoint));
      if (!params.empty()) {
        query.addQueryItem(QStringLiteral("params"),
                           QString::fromStdString(params));
      }
      query.addQueryItem(QStringLiteral("requireAuth"),
                         requireAuth ? QStringLiteral("1")
                                     : QStringLiteral("0"));
      url.setQuery(query);

      std::vector<std::string> headers = {"Accept: application/json"};
      if (!proxySessionId_.empty()) {
        headers.push_back("X-AIO-YouTube-Session: " + proxySessionId_);
      }

      const auto response =
          PerformHttpRequest(url.toString().toStdString(), {}, headers, 30L);
      if (response.curlCode == CURLE_OK && response.statusCode >= 200 &&
          response.statusCode < 300) {
        return response.body;
      }
      if (response.statusCode >= 400) {
        const QString message = JsonMessageFromResponseBody(response.body);
        if (response.statusCode == 401 &&
            IsInvalidProxySessionMessage(message)) {
          clearInvalidProxySessionLocked();
          saveOAuthStateLocked();
        }
        std::cerr << "[YouTube] Proxy request returned HTTP "
                  << response.statusCode << " for endpoint " << endpoint
                  << std::endl;
      }
      return {};
    }
  }
  std::cerr << "[YouTube] YouTube auth server is unavailable. Set "
               "AIO_YOUTUBE_SERVER_AUTOBOOT=1 or AIO_YOUTUBE_SERVER_URL."
            << std::endl;
  return {};
}

namespace {
QString relativeTimeFromISO(const QString &isoDate) {
  QDateTime published = QDateTime::fromString(isoDate, Qt::ISODate);
  if (!published.isValid())
    return QString();
  QDateTime now = QDateTime::currentDateTimeUtc();
  qint64 secs = published.secsTo(now);
  if (secs < 60)
    return QStringLiteral("just now");
  if (secs < 3600)
    return QStringLiteral("%1 minutes ago").arg(secs / 60);
  if (secs < 86400)
    return QStringLiteral("%1 hours ago").arg(secs / 3600);
  if (secs < 2592000)
    return QStringLiteral("%1 days ago").arg(secs / 86400);
  if (secs < 31536000)
    return QStringLiteral("%1 months ago").arg(secs / 2592000);
  return QStringLiteral("%1 years ago").arg(secs / 31536000);
}

int parseDurationISO8601(const QString &duration) {
  QRegularExpression re(R"(PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)");
  auto match = re.match(duration);
  if (!match.hasMatch())
    return 0;
  int hours = match.captured(1).toInt();
  int minutes = match.captured(2).toInt();
  int seconds = match.captured(3).toInt();
  return hours * 3600 + minutes * 60 + seconds;
}
} // namespace

std::vector<VideoContent>
YouTubeService::parseVideoResults(const std::string &jsonResponse) {
  std::vector<VideoContent> results;
  if (jsonResponse.empty()) {
    return results;
  }

  const QByteArray bytes(jsonResponse.data(), (int)jsonResponse.size());
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    std::cerr << "[YouTube] JSON parse failed: "
              << err.errorString().toStdString() << std::endl;
    return results;
  }

  const QJsonObject root = doc.object();
  const QJsonArray items = root.value("items").toArray();
  results.reserve((size_t)items.size());
  QSettings progressSettings(kSettingsOrg, kSettingsApp);

  for (const auto &value : items) {
    if (!value.isObject()) {
      continue;
    }

    const QJsonObject item = value.toObject();
    VideoContent vc;
    vc.durationSeconds = 0;

    const QJsonValue idVal = item.value("id");
    if (idVal.isObject()) {
      vc.id = idVal.toObject().value("videoId").toString().toStdString();
    } else if (idVal.isString()) {
      vc.id = idVal.toString().toStdString();
    }

    const QJsonObject snippet = item.value("snippet").toObject();
    if (!snippet.isEmpty()) {
      vc.title = snippet.value("title").toString().toStdString();
      vc.description = snippet.value("description").toString().toStdString();
      vc.category = snippet.value("channelTitle").toString().toStdString();
      vc.channelName = snippet.value("channelTitle").toString().toStdString();
      const QString publishedISO = snippet.value("publishedAt").toString();
      if (!publishedISO.isEmpty()) {
        vc.publishedAt = relativeTimeFromISO(publishedISO).toStdString();
      }
      if (snippet.value("liveBroadcastContent").toString() ==
          QStringLiteral("live")) {
        vc.isLive = true;
      }
      const QJsonObject thumbs = snippet.value("thumbnails").toObject();
      if (!thumbs.isEmpty()) {
        vc.thumbnailUrl = PickBestThumb(thumbs).toStdString();
      }
    }

    const QJsonObject contentDetails = item.value("contentDetails").toObject();
    if (!contentDetails.isEmpty()) {
      vc.durationSeconds =
          parseDurationISO8601(contentDetails.value("duration").toString());
    }

    if (!vc.id.empty()) {
      vc.videoUrl = "https://www.youtube.com/watch?v=" + vc.id;
      vc.watchProgressSeconds =
          progressSettings
              .value(QStringLiteral("youtube/progress_flat/%1")
                         .arg(QString::fromStdString(vc.id)),
                     0)
              .toInt();
      results.push_back(std::move(vc));
    }
  }

  return results;
}

std::vector<VideoContent>
YouTubeService::parsePlaylistItemResults(const std::string &jsonResponse) {
  std::vector<VideoContent> results;
  if (jsonResponse.empty()) {
    return results;
  }

  const QByteArray bytes(jsonResponse.data(), (int)jsonResponse.size());
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    std::cerr << "[YouTube] Playlist JSON parse failed: "
              << err.errorString().toStdString() << std::endl;
    return results;
  }

  const QJsonArray items = doc.object().value("items").toArray();
  results.reserve((size_t)items.size());
  QSettings progressSettings(kSettingsOrg, kSettingsApp);

  for (const auto &entry : items) {
    if (!entry.isObject()) {
      continue;
    }

    const QJsonObject item = entry.toObject();
    const QJsonObject snippet = item.value("snippet").toObject();
    const QJsonObject resourceId = snippet.value("resourceId").toObject();
    const QJsonObject contentDetails = item.value("contentDetails").toObject();

    const QString videoId = resourceId.value("videoId").toString(
        contentDetails.value("videoId").toString());
    if (videoId.isEmpty()) {
      continue;
    }

    VideoContent vc;
    vc.id = videoId.toStdString();
    vc.title = snippet.value("title").toString().toStdString();
    vc.description = snippet.value("description").toString().toStdString();
    vc.videoUrl = "https://www.youtube.com/watch?v=" + vc.id;
    vc.durationSeconds = 0;
    vc.channelName = snippet.value("channelTitle").toString().toStdString();
    const QString publishedISO = snippet.value("publishedAt").toString();
    if (!publishedISO.isEmpty()) {
      vc.publishedAt = relativeTimeFromISO(publishedISO).toStdString();
    }

    const QJsonObject thumbnails = snippet.value("thumbnails").toObject();
    if (!thumbnails.isEmpty()) {
      vc.thumbnailUrl = PickBestThumb(thumbnails).toStdString();
    }

    vc.watchProgressSeconds =
        progressSettings
            .value(QStringLiteral("youtube/progress_flat/%1")
                       .arg(QString::fromStdString(vc.id)),
                   0)
            .toInt();
    results.push_back(std::move(vc));
  }

  return results;
}

std::vector<VideoContent>
YouTubeService::fetchPlaylistItems(const std::string &playlistId, int limit) {
  if (playlistId.empty()) {
    return {};
  }

  std::string params =
      "part=snippet,contentDetails&playlistId=" + UrlEncodeQuery(playlistId) +
      "&maxResults=" + std::to_string(limit);
  return parsePlaylistItemResults(
      makeApiRequest("playlistItems", params, true));
}

std::vector<VideoContent>
YouTubeService::fetchVideosByIds(const std::vector<std::string> &ids) {
  if (ids.empty()) {
    return {};
  }

  std::string joined;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      joined += ',';
    }
    joined += ids[i];
  }
  return parseVideoResults(
      makeApiRequest("videos", "part=snippet&id=" + joined));
}

std::vector<VideoContent>
YouTubeService::fetchMostPopularByCategory(const std::string &videoCategoryId,
                                           int limit) {
  if (videoCategoryId.empty() || limit <= 0) {
    return {};
  }

  ensureAuthenticatedFromEnvironment();

  std::string region;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    region = regionCode_;
  }

  std::string params =
      "part=snippet,contentDetails&chart=mostPopular&maxResults=" +
      std::to_string(limit) + "&regionCode=" + region +
      "&videoCategoryId=" + UrlEncodeQuery(videoCategoryId);
  return parseVideoResults(makeApiRequest("videos", params));
}

std::string YouTubeService::getMinePlaylistId(const std::string &playlistKey) {
  if (playlistKey.empty()) {
    return {};
  }

  const std::string response =
      makeApiRequest("channels", "part=contentDetails&mine=true", true);
  if (response.empty()) {
    return {};
  }

  const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
  const auto items = doc.object().value("items").toArray();
  if (items.isEmpty()) {
    return {};
  }

  const auto related = items.first()
                           .toObject()
                           .value("contentDetails")
                           .toObject()
                           .value("relatedPlaylists")
                           .toObject();
  return related.value(QString::fromStdString(playlistKey))
      .toString()
      .toStdString();
}

std::vector<std::string> YouTubeService::getSubscribedChannelIds(int limit) {
  const std::string response =
      makeApiRequest("subscriptions",
                     "part=snippet&mine=true&order=unread&maxResults=" +
                         std::to_string(std::clamp(limit, 1, 50)),
                     true);
  if (response.empty()) {
    return {};
  }

  const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(response));
  const auto items = doc.object().value("items").toArray();

  std::vector<std::string> ids;
  ids.reserve((size_t)items.size());
  for (const auto &entry : items) {
    const auto channelId = entry.toObject()
                               .value("snippet")
                               .toObject()
                               .value("resourceId")
                               .toObject()
                               .value("channelId")
                               .toString();
    if (!channelId.isEmpty()) {
      ids.push_back(channelId.toStdString());
    }
  }
  return ids;
}

std::vector<VideoContent>
YouTubeService::getLatestUploadsFromSubscriptions(int limit) {
  const auto channelIds = getSubscribedChannelIds(std::min(limit, 12));
  if (channelIds.empty()) {
    return {};
  }

  std::string joined;
  for (size_t i = 0; i < channelIds.size(); ++i) {
    if (i > 0) {
      joined += ',';
    }
    joined += channelIds[i];
  }

  const std::string channelsResponse =
      makeApiRequest("channels", "part=contentDetails&id=" + joined, true);
  if (channelsResponse.empty()) {
    return {};
  }

  const auto doc =
      QJsonDocument::fromJson(QByteArray::fromStdString(channelsResponse));
  const auto items = doc.object().value("items").toArray();

  std::vector<VideoContent> results;
  std::unordered_set<std::string> seenIds;
  for (const auto &entry : items) {
    const auto uploads = entry.toObject()
                             .value("contentDetails")
                             .toObject()
                             .value("relatedPlaylists")
                             .toObject()
                             .value("uploads")
                             .toString()
                             .toStdString();
    if (uploads.empty()) {
      continue;
    }

    auto videos = fetchPlaylistItems(uploads, 3);
    AppendUniqueVideos(results, seenIds, std::move(videos), limit,
                       "Subscriptions");
    if ((int)results.size() >= limit) {
      break;
    }
  }
  return results;
}

std::vector<VideoContent> YouTubeService::getTrending(int limit) {
  std::cout << "[YouTube] Fetching trending videos (limit: " << limit << ")"
            << std::endl;
  ensureAuthenticatedFromEnvironment();

  std::string region;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    region = regionCode_;
  }

  std::string params =
      "part=snippet,contentDetails&chart=mostPopular&maxResults=" +
      std::to_string(limit) + "&regionCode=" + region;
  return parseVideoResults(makeApiRequest("videos", params));
}

std::vector<VideoContent> YouTubeService::search(const std::string &query,
                                                 int limit) {
  std::cout << "[YouTube] Searching for: " << query << " (limit: " << limit
            << ")" << std::endl;
  ensureAuthenticatedFromEnvironment();

  std::string region;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    region = regionCode_;
  }

  std::string params = "part=snippet&type=video&q=" + UrlEncodeQuery(query) +
                       "&maxResults=" + std::to_string(limit) +
                       "&regionCode=" + region;
  return parseVideoResults(makeApiRequest("search", params));
}

std::vector<VideoContent>
YouTubeService::getRelatedVideos(const std::string &videoId, int limit) {
  std::cout << "[YouTube] Fetching related videos for: " << videoId
            << " (limit: " << limit << ")" << std::endl;

  if (videoId.empty()) {
    return {};
  }

  ensureAuthenticatedFromEnvironment();

  std::string region;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    region = regionCode_;
  }

  std::string metadataParams = "part=snippet&id=" + UrlEncodeQuery(videoId);
  const std::string metadataResponse = makeApiRequest("videos", metadataParams);

  QString relatedQuery;
  if (!metadataResponse.empty()) {
    const QByteArray bytes(metadataResponse.data(),
                           static_cast<int>(metadataResponse.size()));
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
      const QJsonArray items = doc.object().value("items").toArray();
      if (!items.isEmpty() && items.first().isObject()) {
        const QJsonObject snippet =
            items.first().toObject().value("snippet").toObject();
        const QString title = snippet.value("title").toString().simplified();
        const QString channel =
            snippet.value("channelTitle").toString().simplified();
        if (!title.isEmpty() && !channel.isEmpty()) {
          relatedQuery = QStringLiteral("%1 %2").arg(channel, title);
        } else if (!title.isEmpty()) {
          relatedQuery = title;
        } else if (!channel.isEmpty()) {
          relatedQuery = channel;
        }
      }
    }
  }

  if (relatedQuery.isEmpty()) {
    auto fallback = getTrending(limit);
    for (auto &item : fallback) {
      item.category = "Trending";
    }
    return fallback;
  }

  std::string params = "part=snippet&type=video&q=" +
                       UrlEncodeQuery(relatedQuery.toStdString()) +
                       "&maxResults=" + std::to_string(limit);
  auto results = parseVideoResults(makeApiRequest("search", params));
  results.erase(std::remove_if(results.begin(), results.end(),
                               [&](const VideoContent &item) {
                                 return item.id == videoId;
                               }),
                results.end());
  if (results.empty()) {
    auto fallback = getTrending(limit);
    for (auto &item : fallback) {
      item.category = "Trending";
    }
    return fallback;
  }
  return results;
}

std::vector<VideoContent> YouTubeService::getRecommended(int limit) {
  std::cout << "[YouTube] Fetching recommended videos (limit: " << limit << ")"
            << std::endl;

  if (!hasOAuthAccess()) {
    auto items = getTrending(limit);
    for (auto &item : items) {
      item.category = "Trending";
    }
    return items;
  }

  std::vector<VideoContent> feed;
  std::unordered_set<std::string> seenIds;

  AppendUniqueVideos(feed, seenIds, getContinueWatching(), limit,
                     "Continue watching");
  AppendUniqueVideos(feed, seenIds, getWatchLater(std::min(limit, 10)), limit,
                     "Watch later");
  AppendUniqueVideos(feed, seenIds, getLikedVideos(std::min(limit, 10)), limit,
                     "Liked videos");
  AppendUniqueVideos(feed, seenIds, getSubscriptionFeed(std::min(limit, 14)),
                     limit, "Subscriptions");

  if ((int)feed.size() < limit) {
    auto fallback = getTrending(limit - (int)feed.size());
    AppendUniqueVideos(feed, seenIds, std::move(fallback), limit, "Trending");
  }

  return feed;
}

std::vector<VideoContent> YouTubeService::getContinueWatching() {
  std::cout << "[YouTube] Fetching continue watching list" << std::endl;

  QSettings settings(kSettingsOrg, kSettingsApp);
  const int size = settings.beginReadArray("youtube/progress");
  struct ProgressEntry {
    std::string id;
    int positionSeconds = 0;
    qint64 updatedAt = 0;
  };
  std::vector<ProgressEntry> progressEntries;
  progressEntries.reserve(size_t(size));
  for (int i = 0; i < size; ++i) {
    settings.setArrayIndex(i);
    const QString id = settings.value("videoId").toString();
    if (id.isEmpty()) {
      continue;
    }
    progressEntries.push_back({
        id.toStdString(),
        settings.value("positionSeconds").toInt(),
        settings.value("updatedAt").toLongLong(),
    });
  }
  settings.endArray();

  std::sort(progressEntries.begin(), progressEntries.end(),
            [](const ProgressEntry &lhs, const ProgressEntry &rhs) {
              return lhs.updatedAt > rhs.updatedAt;
            });

  std::vector<std::string> ids;
  for (const auto &entry : progressEntries) {
    if (entry.positionSeconds > 0) {
      ids.push_back(entry.id);
    }
    if ((int)ids.size() >= 12) {
      break;
    }
  }

  auto results = fetchVideosByIds(ids);
  for (auto &item : results) {
    item.category = "Continue watching";
  }
  if (!results.empty()) {
    return results;
  }

  if (!hasOAuthAccess()) {
    return {};
  }

  const std::string watchHistoryId = getMinePlaylistId("watchHistory");
  auto history = fetchPlaylistItems(watchHistoryId, 10);
  for (auto &item : history) {
    item.category = "Recent history";
  }
  return history;
}

std::vector<VideoContent> YouTubeService::getWatchLater(int limit) {
  if (!hasOAuthAccess()) {
    return {};
  }
  auto items = fetchPlaylistItems(getMinePlaylistId("watchLater"), limit);
  for (auto &item : items) {
    item.category = "Watch later";
  }
  return items;
}

std::vector<VideoContent> YouTubeService::getLikedVideos(int limit) {
  if (!hasOAuthAccess()) {
    return {};
  }
  auto items = fetchPlaylistItems(getMinePlaylistId("likes"), limit);
  for (auto &item : items) {
    item.category = "Liked videos";
  }
  return items;
}

std::vector<VideoContent> YouTubeService::getSubscriptionFeed(int limit) {
  if (!hasOAuthAccess()) {
    return {};
  }
  return getLatestUploadsFromSubscriptions(limit);
}

std::vector<YouTubeContentRail>
YouTubeService::getHomeRails(int itemsPerRail, int discoveryDepth) {
  const int limit = std::clamp(itemsPerRail, 12, 40);
  std::vector<YouTubeContentRail> rails;
  std::unordered_set<std::string> usedFirstIds;
  const auto trendingPool = getTrending(limit * 2);

  auto trimForRail = [&](std::vector<VideoContent> items) {
    std::vector<VideoContent> trimmed;
    trimmed.reserve(std::min(limit, static_cast<int>(items.size())));
    for (auto &item : items) {
      if (item.id.empty() || usedFirstIds.contains(item.id)) {
        continue;
      }
      usedFirstIds.insert(item.id);
      trimmed.push_back(std::move(item));
      if (static_cast<int>(trimmed.size()) >= limit) {
        break;
      }
    }
    return trimmed;
  };

  auto sliceItems = [](const std::vector<VideoContent> &items, int offset,
                       int count) {
    std::vector<VideoContent> slice;
    if (offset < 0 || offset >= static_cast<int>(items.size()) || count <= 0) {
      return slice;
    }
    const int end = std::min(offset + count, static_cast<int>(items.size()));
    slice.reserve(end - offset);
    for (int index = offset; index < end; ++index) {
      slice.push_back(items[index]);
    }
    return slice;
  };

  auto addRail = [&](const std::string &key, const std::string &title,
                     const std::string &subtitle,
                     std::vector<VideoContent> items) {
    auto trimmed = trimForRail(std::move(items));
    if (trimmed.empty()) {
      return;
    }
    rails.push_back({key, title, subtitle, std::move(trimmed)});
  };

  bool firstCategory = true;
  auto addCategoryRail = [&](const std::string &key, const std::string &title,
                             const std::string &subtitle,
                             const std::string &categoryQuery) {
    if (!firstCategory) {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    firstCategory = false;
    auto items = search(categoryQuery, limit + 4);
    if (items.empty()) {
      std::cout << "[YouTube] Category rail '" << key
                << "' returned no results, skipping" << std::endl;
      return;
    }
    addRail(key, title, subtitle, std::move(items));
  };

  if (hasOAuthAccess()) {
    auto recommended = getRecommended(limit * 3);
    addRail("recommended", "Recommended for you",
            "A fuller mix from watch progress, subscriptions, saved videos, "
            "and related picks.",
            sliceItems(recommended, 0, limit));
    addRail("recommended_more", "Keep watching",
            "More picks pulled from the same signed-in recommendation feed.",
            sliceItems(recommended, limit, limit));
    addRail("continue", "Continue watching",
            "Jump back into videos you already started.",
            getContinueWatching());
    addRail("subscriptions", "From your subscriptions",
            "Fresh uploads from channels you already follow.",
            getSubscriptionFeed(limit + 4));
    addRail("watch_later", "Watch later",
            "Saved clips and longer sessions waiting for you.",
            getWatchLater(limit));
    addRail("liked", "Liked videos",
            "Things you explicitly told YouTube you wanted more of.",
            getLikedVideos(limit));
    addCategoryRail("gaming", "Gaming on YouTube",
                    "Most popular gaming videos in the public catalog.",
                    "gaming");
    addCategoryRail("music", "Music and live sets",
                    "Most popular music videos and performances right now.",
                    "music");
  } else {
    addRail("recommended", "Popular on YouTube",
            "A broad cross-section of what is active on the public catalog "
            "right now.",
            sliceItems(trendingPool, 0, limit));
    addCategoryRail("gaming", "Gaming spotlight",
                    "Console-friendly picks from the gaming charts.", "gaming");
    addCategoryRail("music", "Music and performances",
                    "Most popular music videos and live sets.", "music");
    addCategoryRail("news", "News and commentary",
                    "Most popular news and politics videos right now.", "news");
    addCategoryRail(
        "creators", "Entertainment and creators",
        "Popular entertainment videos without spending search quota.",
        "entertainment");
  }

  auto trending = sliceItems(trendingPool, limit, limit);
  if (trending.empty()) {
    trending = sliceItems(trendingPool, 0, limit);
  }
  for (auto &item : trending) {
    item.category = "Trending";
  }
  addRail(
      "trending", hasOAuthAccess() ? "Trending now" : "Trending now",
      hasOAuthAccess()
          ? "The public chart, kept as a fallback and discovery row."
          : "A dedicated trending row alongside broader signed-out discovery.",
      std::move(trending));

  const int extraRailCount =
      std::clamp(discoveryDepth, 0,
                 static_cast<int>(std::size(kExtraDiscoveryRails))) *
      kExtraDiscoveryRailsPerLevel;
  const int boundedExtraRailCount = std::min(
      extraRailCount, static_cast<int>(std::size(kExtraDiscoveryRails)));
  for (int index = 0; index < boundedExtraRailCount; ++index) {
    const auto &extra = kExtraDiscoveryRails[index];
    addCategoryRail(extra.key, extra.title, extra.subtitle, extra.key);
  }

  return rails;
}

std::string YouTubeService::getStreamUrl(const std::string &contentId) {
  std::cout << "[YouTube] Getting stream URL for: " << contentId << std::endl;
  return "https://www.youtube.com/watch?v=" + contentId;
}

bool YouTubeService::startPlayback(const std::string &contentId) {
  std::cout << "[YouTube] Starting playback for: " << contentId << std::endl;
  return true;
}

void YouTubeService::updateWatchProgress(const std::string &contentId,
                                         int positionSeconds) {
  std::cout << "[YouTube] Updating watch progress: " << contentId << " @ "
            << positionSeconds << "s" << std::endl;

  if (contentId.empty() || positionSeconds <= 0) {
    return;
  }

  // Persist flat key for fast per-video thumbnail progress lookup.
  if (positionSeconds > 5) {
    QSettings flatSettings(kSettingsOrg, kSettingsApp);
    flatSettings.setValue(QStringLiteral("youtube/progress_flat/%1")
                              .arg(QString::fromStdString(contentId)),
                          positionSeconds);
  }

  QSettings settings(kSettingsOrg, kSettingsApp);
  const int size = settings.beginReadArray("youtube/progress");
  struct ProgressEntry {
    QString videoId;
    int positionSeconds = 0;
    qint64 updatedAt = 0;
  };
  std::vector<ProgressEntry> entries;
  entries.reserve(size_t(size));
  for (int i = 0; i < size; ++i) {
    settings.setArrayIndex(i);
    const QString videoId = settings.value("videoId").toString();
    if (videoId.isEmpty()) {
      continue;
    }
    entries.push_back({
        videoId,
        settings.value("positionSeconds").toInt(),
        settings.value("updatedAt").toLongLong(),
    });
  }
  settings.endArray();

  const QString targetId = QString::fromStdString(contentId);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  bool updated = false;
  for (auto &entry : entries) {
    if (entry.videoId == targetId) {
      entry.positionSeconds = positionSeconds;
      entry.updatedAt = now;
      updated = true;
      break;
    }
  }
  if (!updated) {
    entries.push_back({targetId, positionSeconds, now});
  }

  std::sort(entries.begin(), entries.end(),
            [](const ProgressEntry &lhs, const ProgressEntry &rhs) {
              return lhs.updatedAt > rhs.updatedAt;
            });
  if (entries.size() > 30) {
    entries.resize(30);
  }

  settings.beginWriteArray("youtube/progress");
  for (int i = 0; i < (int)entries.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue("videoId", entries[i].videoId);
    settings.setValue("positionSeconds", entries[i].positionSeconds);
    settings.setValue("updatedAt", entries[i].updatedAt);
  }
  settings.endArray();
}

void YouTubeService::fetchSearchSuggestionsAsync(
    const std::string &query,
    std::function<void(std::vector<std::string>)> callback) {
  if (query.empty()) {
    callback({});
    return;
  }
  const QString q = QString::fromStdString(query);
  std::vector<std::string> suggestions;
  suggestions.push_back(query);
  suggestions.push_back((q + " official").toStdString());
  suggestions.push_back((q + " live").toStdString());
  suggestions.push_back((q + " full video").toStdString());
  callback(std::move(suggestions));
}

} // namespace Streaming
} // namespace AIO
