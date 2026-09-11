#pragma once

#include "ipc_service.hpp"

namespace nemu::core::kernel::ipc {

/// sm: service. Implements the Horizon service-manager interface: Initialize,
/// GetServiceHandle, RegisterService, UnregisterService. GetServiceHandle
/// resolves the requested service name against the ServiceRegistry and returns
/// a newly created KClientPort handle in the caller's handle table.
class SmService final : public IIpcService {
public:
    SmService();

    /// Command IDs.
    enum : u32 {
        Initialize = 0x0,
        GetServiceHandle = 0x1,
        RegisterService = 0x2,
        UnregisterService = 0x3,
        GetServiceHandleDeprecated = 0x4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    u32 HandleInitialize(const IpcContext& ctx, IpcReplyWriter& reply);
    u32 HandleGetServiceHandle(const IpcContext& ctx, const IpcRequestReader& request,
                               IpcReplyWriter& reply);
    u32 HandleRegisterService(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply);
    u32 HandleUnregisterService(const IpcContext& ctx, const IpcRequestReader& request,
                                IpcReplyWriter& reply);
};

} // namespace nemu::core::kernel::ipc