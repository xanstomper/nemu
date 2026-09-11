#pragma once

#include "ipc_service.hpp"
#include "ipc_types.hpp"
#include <memory>
#include <shared_mutex>
#include <string>
#include <optional>
#include <unordered_map>

namespace nemu::core::kernel::ipc {

/// The Horizon service manager (`sm:` HLE). Owns the set of registered HLE
/// services and answers by service name, mirroring libnx `smGetServiceHandle`.
class ServiceRegistry {
public:
    ServiceRegistry() = default;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    /// Register an HLE service under its Horizon name (e.g. "sm:", "time:u").
    /// Returns false if a service with that name is already registered.
    bool Register(std::shared_ptr<IIpcService> service);

    /// True if a service with the given name is registered.
    [[nodiscard]] bool IsRegistered(const std::string& name) const;

    /// Look up a registered service by name.
    [[nodiscard]] std::shared_ptr<IIpcService> Find(const std::string& name) const;

    /// Number of registered services.
    [[nodiscard]] size_t Count() const;

    /// Create a client port handle (as an in-memory object, ready to be added
    /// to a KHandleTable) bound to the named service. Returns nullopt if the
    /// service is unknown.
    [[nodiscard]] std::optional<std::shared_ptr<KClientPort>> CreatePort(const std::string& name) const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<IIpcService>> services_;
};

} // namespace nemu::core::kernel::ipc