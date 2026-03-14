#ifndef STREAMING_SERVICE_H
#define STREAMING_SERVICE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AIO {
namespace Streaming {

struct VideoContent {
  std::string id;
  std::string title;
  std::string description;
  std::string thumbnailUrl;
  std::string videoUrl;
  int durationSeconds;
  std::string category;
  int watchProgressSeconds = 0;
  std::string channelName;
  std::string viewCount;
  std::string publishedAt;
  bool isLive = false;
};

struct StreamingCredentials {
  std::string username;
  std::string password;
  std::string apiKey;
  std::string accessToken;
  std::string refreshToken;
};

enum class StreamingServiceType { YouTube, Netflix, DisneyPlus, Hulu };

class IStreamingService {
public:
  virtual ~IStreamingService() = default;

  virtual bool authenticate(const StreamingCredentials &creds) = 0;
  virtual bool isAuthenticated() const = 0;
  virtual void logout() = 0;

  virtual std::vector<VideoContent> getTrending(int limit = 20) = 0;
  virtual std::vector<VideoContent> search(const std::string &query,
                                           int limit = 20) = 0;
  virtual std::vector<VideoContent> getRecommended(int limit = 20) = 0;
  virtual std::vector<VideoContent> getContinueWatching() = 0;

  virtual std::string getStreamUrl(const std::string &contentId) = 0;
  virtual bool startPlayback(const std::string &contentId) = 0;
  virtual void updateWatchProgress(const std::string &contentId,
                                   int positionSeconds) = 0;

  virtual std::string getServiceName() const = 0;
  virtual StreamingServiceType getServiceType() const = 0;
};

} // namespace Streaming
} // namespace AIO

#endif // STREAMING_SERVICE_H
