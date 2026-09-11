#include "service_registry.hpp"
#include <mutex>

namespace nemu::core::kernel::ipc {

bool ServiceRegistry::Register(std::shared_ptr<IIpcService> service) {
    if (!service) {
        return false;
    }
    std::unique_lock lock(mutex_);
    return services_.emplace(service->GetName(), std::move(service)).second;
}

bool ServiceRegistry::IsRegistered(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return services_.find(name) != services_.end();
}

std::shared_ptr<IIpcService> ServiceRegistry::Find(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = services_.find(name);
    return it == services_.end() ? std::shared_ptr<IIpcService>{} : it->second;
}

size_t ServiceRegistry::Count() const {
    std::shared_lock lock(mutex_);
    return services_.size();
}

std::optional<std::shared_ptr<KClientPort>> ServiceRegistry::CreatePort(const std::string& name) const {
    auto service = Find(name);
    if (!service) {
        return std::nullopt;
    }
    auto port = std::make_shared<KClientPort>();
    port->SetService(std::move(service));
    return port;
}

} // namespace nemu::core::kernel::ipc