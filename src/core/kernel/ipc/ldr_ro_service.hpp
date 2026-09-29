#pragma once

#include "ipc_service.hpp"
#include <string>

namespace nemu::core::kernel::ipc {

/// ldr:ro - Relocatable Object Loader Service.
/// Used to dynamically load and link NRO homebrew and plugins in games.
class LdrRoService final : public IIpcService {
public:
    explicit LdrRoService(std::string name = "ldr:ro");
    ~LdrRoService() override = default;

    enum Commands : u32 {
        LoadNro = 0,
        UnloadNro = 1,
        LoadNrr = 2,
        UnloadNrr = 3,
        Initialize = 4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
