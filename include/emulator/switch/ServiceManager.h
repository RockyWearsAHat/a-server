#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>

namespace AIO::Emulator::Switch {

    class ServiceManager;

    // Base class for all HLE services
    class Service {
    public:
        virtual ~Service() = default;
        virtual void Dispatch(ServiceManager& sm, uint32_t commandId) = 0;
        virtual std::string GetName() const = 0;
    };

    class ServiceManager {
    public:
        ServiceManager();
        ~ServiceManager();

        void RegisterService(const std::string& name, std::shared_ptr<Service> service);
        std::shared_ptr<Service> GetService(const std::string& name);
        
        // Handle IPC Command
        void SendSyncRequest(uint32_t handle);

    private:
        std::map<std::string, std::shared_ptr<Service>> services;
        std::map<uint32_t, std::shared_ptr<Service>> sessions;
        uint32_t nextHandle = 1;
    };

}
