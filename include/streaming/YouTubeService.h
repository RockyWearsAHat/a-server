#ifndef YOUTUBE_SERVICE_H
#define YOUTUBE_SERVICE_H

#include "StreamingService.h"
#include <mutex>

namespace AIO {
namespace Streaming {

class YouTubeService : public IStreamingService {
public:
    YouTubeService();
    ~YouTubeService() override = default;
    
    bool authenticate(const StreamingCredentials& creds) override;
    bool isAuthenticated() const override;
    void logout() override;
    
    std::vector<VideoContent> getTrending(int limit = 20) override;
    std::vector<VideoContent> search(const std::string& query, int limit = 20) override;
    std::vector<VideoContent> getRecommended(int limit = 20) override;
    std::vector<VideoContent> getContinueWatching() override;
    
    std::string getStreamUrl(const std::string& contentId) override;
    bool startPlayback(const std::string& contentId) override;
    void updateWatchProgress(const std::string& contentId, int positionSeconds) override;
    
    std::string getServiceName() const override { return "YouTube"; }
    StreamingServiceType getServiceType() const override { return StreamingServiceType::YouTube; }

private:
    void ensureAuthenticatedFromEnvironment();
    std::string makeApiRequest(const std::string& endpoint, const std::string& params = "");
    std::vector<VideoContent> parseVideoResults(const std::string& jsonResponse);

    std::string regionCode_ = "US";
    
    std::string apiKey_;
    std::string accessToken_;
    bool authenticated_;
    mutable std::mutex mutex_;
};

} // namespace Streaming
} // namespace AIO

#endif // YOUTUBE_SERVICE_H
