#include "emulator/switch/ServiceManager.h"
#include <iostream>

namespace AIO::Emulator::Switch {

    ServiceManager::ServiceManager() {
        // Register default services here later
    }

    ServiceManager::~ServiceManager() = default;

    void ServiceManager::RegisterService(const std::string& name, std::shared_ptr<Service> service) {
        services[name] = service;
        std::cout << "[HLE] Registered Service: " << name << std::endl;
    }

    std::shared_ptr<Service> ServiceManager::GetService(const std::string& name) {
        if (services.find(name) != services.end()) {
            return services[name];
        }
        return nullptr;
    }

    void ServiceManager::SendSyncRequest(uint32_t handle) {
        // In a real emulator, we would look up the session by handle
        // and dispatch the command from the IPC buffer.
        // For now, just log it.
        std::cout << "[HLE] SendSyncRequest on Handle: 0x" << std::hex << handle << std::dec << std::endl;
    }

}
