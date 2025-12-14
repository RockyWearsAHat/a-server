#ifndef HULU_SERVICE_H
#define HULU_SERVICE_H

#include "StreamingService.h"
#include <mutex>

namespace AIO {
namespace Streaming {

class HuluService : public IStreamingService {
public:
    HuluService();
    ~HuluService() override = default;
    
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
    
    std::string getServiceName() const override { return "Hulu"; }
    StreamingServiceType getServiceType() const override { return StreamingServiceType::Hulu; }

private:
    std::string makeApiRequest(const std::string& endpoint, const std::string& params = "");
    std::vector<VideoContent> parseContentResults(const std::string& jsonResponse);
    
    std::string sessionId_;
    std::string userId_;
    bool authenticated_;
    mutable std::mutex mutex_;
};

} // namespace Streaming
} // namespace AIO

#endif // HULU_SERVICE_H
