#ifndef STREAMING_MANAGER_H
#define STREAMING_MANAGER_H

#include "StreamingService.h"
#include <map>
#include <memory>

namespace AIO {
namespace Streaming {

class StreamingManager {
public:
    static StreamingManager& getInstance();
    
    // Service management
    void registerService(StreamingServiceType type, std::shared_ptr<IStreamingService> service);
    std::shared_ptr<IStreamingService> getService(StreamingServiceType type);
    std::vector<StreamingServiceType> getAvailableServices() const;
    
    // Quick access
    bool authenticateService(StreamingServiceType type, const StreamingCredentials& creds);
    bool isServiceAuthenticated(StreamingServiceType type) const;
    void logoutService(StreamingServiceType type);
    
private:
    StreamingManager();
    ~StreamingManager() = default;
    StreamingManager(const StreamingManager&) = delete;
    StreamingManager& operator=(const StreamingManager&) = delete;
    
    std::map<StreamingServiceType, std::shared_ptr<IStreamingService>> services_;
};

} // namespace Streaming
} // namespace AIO

#endif // STREAMING_MANAGER_H
