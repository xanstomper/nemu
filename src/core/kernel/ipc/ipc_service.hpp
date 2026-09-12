#pragma once

#include "core/types.hpp"
#include "core/kernel/k_auto_object.hpp"
#include "ipc_types.hpp"
#include <string>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace nemu::core::kernel {
class KHandleTable;
} // namespace nemu::core::kernel

namespace nemu::core::memory {
class VirtualMemory;
} // namespace nemu::core::memory

namespace nemu::core::kernel::ipc {

class ServiceRegistry;

/// IPC command-buffer command id sentinel values handled by the engine itself.
namespace CommandId {
    inline constexpr u32 CloseSession = 0x0000'FFFF;
}

/// Result code returned by an HLE service handler. `Success` (0) means the
/// command was handled and the reply buffer has been populated. Non-zero maps
/// to a Horizon-style error code surfaced in the guest's X0 on return from
/// svcSendSyncRequest.
enum class IpcResult : u32 {
    Success = 0,
    InvalidRequest = 0xF101,
    Unimplemented = 0xF201,
    InvalidBuffer = 0xF301,
    NotFound = 0xF401,
    PermissionDenied = 0xF501,
    OutOfMemory = 0xCE01,
};

/// Payload produced by a service handler and written back to the guest.
struct IpcReply final {
    u32 type{0};
    u64 data_size{0};
    u8 buffer[ipc::IpcBufferSize]{};

    IpcReply() {
        // Ensure the type field lives at offset 0 of our mirror buffer.
        std::memset(buffer, 0, sizeof(buffer));
    }

    IpcReply(u32 command_type, u64 payload_size)
        : type(command_type), data_size(payload_size) {
        std::memset(buffer, 0, sizeof(buffer));
    }
};

/// Runtime context handed to an HLE service handler so it can fabricate guest
/// handles (for sm: GetServiceHandle), query the service registry, and access
/// guest memory through the process's virtual-memory object.
struct IpcContext {
    kernel::KHandleTable* handle_table{nullptr};
    memory::VirtualMemory* memory{nullptr};
    ServiceRegistry* registry{nullptr};
};

/// Base class for all Horizon HLE services (e.g. "sm:", "time:u", "set:sys").
class IIpcService {
public:
    explicit IIpcService(std::string name) : name_(std::move(name)) {}
    virtual ~IIpcService() = default;

    IIpcService(const IIpcService&) = delete;
    IIpcService& operator=(const IIpcService&) = delete;

    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }

    /// Handle a request decoded from the guest IPC command buffer. The handler
    /// is free to read arguments via `request`, use `ctx` for handle/registry
    /// and memory access, and populate `reply` (the output command buffer)
    /// before the session writes it back to guest memory. Returns the numeric
    /// Horizon result code (0 == success).
    virtual u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) = 0;

    /// Handle a kernel-level control request (e.g. querying the service name).
    /// Default returns Success with no payload.
    virtual u32 HandleControl(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
        (void)ctx;
        (void)request;
        (void)reply;
        (void)x_id;
        return static_cast<u32>(IpcResult::Success);
    }

private:
    std::string name_;
};

// ---------------------------------------------------------------------------
// IPC endpoint kernel objects.
//
// Real Horizon models service access through a chain of objects: a client port
// (obtained from sm:), a server port, and client/server sessions. Here we
// retain the essential objects that survive in the guest handle table.
// ---------------------------------------------------------------------------

/// A client port handle that names a registered HLE service. Connecting to it
/// produces a client session.
class KClientPort final : public KAutoObject {
public:
    KClientPort() : KAutoObject(HandleType::Port) {}

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KClientPort"; }

    void SetService(std::shared_ptr<IIpcService> service) { service_ = std::move(service); }
    [[nodiscard]] const std::shared_ptr<IIpcService>& GetService() const noexcept { return service_; }
    [[nodiscard]] const std::string& GetServiceName() const noexcept { return service_->GetName(); }

private:
    std::shared_ptr<IIpcService> service_;
};

/// A client session handle. Obtained from svcConnectToPort; consumed by
/// svcSendSyncRequest to invoke the underlying HLE service.
class KClientSession final : public KAutoObject {
public:
    KClientSession() : KAutoObject(HandleType::Session) {}

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KClientSession"; }

    void SetService(std::shared_ptr<IIpcService> service) { service_ = std::move(service); }
    [[nodiscard]] const std::shared_ptr<IIpcService>& GetService() const noexcept { return service_; }
    [[nodiscard]] const std::string& GetServiceName() const noexcept { return service_->GetName(); }

    [[nodiscard]] bool IsDomain() const noexcept { return is_domain_; }

    u32 ConvertToDomain() {
        if (!is_domain_) {
            is_domain_ = true;
            if (service_) {
                domain_objects_[1] = service_;
                next_domain_object_id_ = 2;
            }
        }
        return 1;
    }

    u32 RegisterDomainObject(std::shared_ptr<IIpcService> object) {
        u32 id = next_domain_object_id_++;
        domain_objects_[id] = std::move(object);
        return id;
    }

    [[nodiscard]] std::shared_ptr<IIpcService> GetDomainObject(u32 object_id) const {
        if (object_id == 0 || object_id == 1) {
            return service_;
        }
        auto it = domain_objects_.find(object_id);
        if (it != domain_objects_.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool CloseDomainObject(u32 object_id) {
        return domain_objects_.erase(object_id) > 0;
    }

    [[nodiscard]] size_t GetDomainObjectCount() const noexcept {
        return domain_objects_.size();
    }

private:
    std::shared_ptr<IIpcService> service_;
    bool is_domain_{false};
    u32 next_domain_object_id_{1};
    std::unordered_map<u32, std::shared_ptr<IIpcService>> domain_objects_;
};

} // namespace nemu::core::kernel::ipc