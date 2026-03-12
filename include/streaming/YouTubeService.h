#ifndef YOUTUBE_SERVICE_H
#define YOUTUBE_SERVICE_H

#include "StreamingService.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace AIO {
namespace Streaming {

struct YouTubeContentRail {
  std::string key;
  std::string title;
  std::string subtitle;
  std::vector<VideoContent> items;
};

struct YouTubeDeviceAuthSession {
  bool available = false;
  bool active = false;
  bool authenticated = false;
  bool needsClientConfiguration = false;
  std::string verificationUrl;
  std::string verificationUrlComplete;
  std::string userCode;
  std::string statusMessage;
  int pollIntervalSeconds = 5;
  int expiresInSeconds = 0;
  int secondsRemaining = 0;
};

struct YouTubeResolvedStream {
  std::string streamUrl;
  std::string title;
  int durationSeconds = 0;
  std::string thumbnailUrl;
  std::string webpageUrl;
};

class YouTubeService : public IStreamingService {
public:
  YouTubeService();
  ~YouTubeService() override = default;

  bool authenticate(const StreamingCredentials &creds) override;
  bool isAuthenticated() const override;
  void logout() override;

  std::vector<VideoContent> getTrending(int limit = 20) override;
  std::vector<VideoContent> search(const std::string &query,
                                   int limit = 20) override;
  std::vector<VideoContent> getRecommended(int limit = 20) override;
  std::vector<VideoContent> getContinueWatching() override;
  std::vector<VideoContent> getRelatedVideos(const std::string &videoId,
                                             int limit = 20);
  std::vector<VideoContent> getWatchLater(int limit = 20);
  std::vector<VideoContent> getLikedVideos(int limit = 20);
  std::vector<VideoContent> getSubscriptionFeed(int limit = 20);
  std::vector<YouTubeContentRail> getHomeRails(int itemsPerRail = 10,
                                               int discoveryDepth = 0);
  bool hasOAuthAccess() const;
  bool hasDeviceAuthClient() const;
  YouTubeDeviceAuthSession beginDeviceAuth();
  YouTubeDeviceAuthSession pollDeviceAuth();
  void cancelDeviceAuth();
  std::string getAccountDisplayName();
  std::string getAccountAvatarUrl();
  std::optional<YouTubeResolvedStream>
  resolvePlaybackStream(const std::string &videoId);

  std::string getStreamUrl(const std::string &contentId) override;
  bool startPlayback(const std::string &contentId) override;
  void updateWatchProgress(const std::string &contentId,
                           int positionSeconds) override;

  std::string getServiceName() const override { return "YouTube"; }
  StreamingServiceType getServiceType() const override {
    return StreamingServiceType::YouTube;
  }

private:
  void ensureAuthenticatedFromEnvironment();
  void loadStoredStateLocked();
  void saveOAuthStateLocked() const;
  void clearOAuthStateLocked();
  void clearInvalidProxySessionLocked();
  bool refreshAccessTokenLocked();
  bool revokeTokenBestEffort(const std::string &token) const;
  bool usingProxyServerLocked() const;
  std::string makeApiRequest(const std::string &endpoint,
                             const std::string &params = "",
                             bool requireAuth = false);
  std::vector<VideoContent> parseVideoResults(const std::string &jsonResponse);
  std::vector<VideoContent>
  parsePlaylistItemResults(const std::string &jsonResponse);
  std::vector<VideoContent> fetchPlaylistItems(const std::string &playlistId,
                                               int limit);
  std::vector<VideoContent>
  fetchVideosByIds(const std::vector<std::string> &ids);
  std::vector<VideoContent>
  fetchMostPopularByCategory(const std::string &videoCategoryId, int limit);
  std::string getMinePlaylistId(const std::string &playlistKey);
  std::vector<std::string> getSubscribedChannelIds(int limit);
  std::vector<VideoContent> getLatestUploadsFromSubscriptions(int limit);

  std::string regionCode_ = "US";

  std::string apiKey_;
  std::string proxyBaseUrl_;
  std::string proxySessionId_;
  std::string accessToken_;
  std::string refreshToken_;
  std::string oauthClientId_;
  std::string oauthClientSecret_;
  std::string accountDisplayName_;
  std::string accountAvatarUrl_;
  bool stateLoaded_ = false;
  bool authenticated_;

  struct PendingDeviceAuth {
    bool active = false;
    std::string deviceCode;
    std::string userCode;
    std::string verificationUrl;
    std::string verificationUrlComplete;
    std::string statusMessage;
    int pollIntervalSeconds = 5;
    int expiresInSeconds = 0;
    std::int64_t createdAtSeconds = 0;
  } pendingDeviceAuth_;

  mutable std::mutex mutex_;
};

} // namespace Streaming
} // namespace AIO

#endif // YOUTUBE_SERVICE_H
