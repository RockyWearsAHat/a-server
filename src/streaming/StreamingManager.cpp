#include "streaming/StreamingManager.h"
#include "streaming/YouTubeService.h"
#include "streaming/NetflixService.h"
#include "streaming/DisneyPlusService.h"
#include "streaming/HuluService.h"
#include <iostream>

namespace AIO {
namespace Streaming {

StreamingManager& StreamingManager::getInstance() {
    static StreamingManager instance;
    return instance;
}

StreamingManager::StreamingManager() {
    // Register all available services
    registerService(StreamingServiceType::YouTube, std::make_shared<YouTubeService>());
    registerService(StreamingServiceType::Netflix, std::make_shared<NetflixService>());
    registerService(StreamingServiceType::DisneyPlus, std::make_shared<DisneyPlusService>());
    registerService(StreamingServiceType::Hulu, std::make_shared<HuluService>());
    
    std::cout << "[StreamingManager] Initialized with " << services_.size() << " services" << std::endl;
}

void StreamingManager::registerService(StreamingServiceType type, std::shared_ptr<IStreamingService> service) {
    services_[type] = service;
    std::cout << "[StreamingManager] Registered service: " << service->getServiceName() << std::endl;
}

std::shared_ptr<IStreamingService> StreamingManager::getService(StreamingServiceType type) {
    auto it = services_.find(type);
    if (it != services_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<StreamingServiceType> StreamingManager::getAvailableServices() const {
    std::vector<StreamingServiceType> types;
    for (const auto& pair : services_) {
        types.push_back(pair.first);
    }
    return types;
}

bool StreamingManager::authenticateService(StreamingServiceType type, const StreamingCredentials& creds) {
    auto service = getService(type);
    if (service) {
        return service->authenticate(creds);
    }
    std::cerr << "[StreamingManager] Service not found" << std::endl;
    return false;
}

bool StreamingManager::isServiceAuthenticated(StreamingServiceType type) const {
    auto it = services_.find(type);
    if (it != services_.end()) {
        return it->second->isAuthenticated();
    }
    return false;
}

void StreamingManager::logoutService(StreamingServiceType type) {
    auto service = getService(type);
    if (service) {
        service->logout();
    }
}

} // namespace Streaming
} // namespace AIO
