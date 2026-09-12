#pragma once

#include "ipc_service.hpp"
#include "core/types.hpp"
#include <vector>
#include <string>

namespace nemu::core::kernel::ipc {

enum class SharedFontType : u32 {
    Standard = 0,
    ChineseSimplified = 1,
    ExtChineseSimplified = 2,
    ChineseTraditional = 3,
    Korean = 4,
    NintendoExtension = 5,
    TotalFonts = 6
};

class PlService final : public IIpcService {
public:
    explicit PlService(std::string name = "pl:u");
    ~PlService() override = default;

    enum Commands : u32 {
        RequestSharedFont = 0,
        GetSharedFontLoadState = 1,
        GetSharedFontSize = 2,
        GetSharedFontAddress = 3,
        GetSharedFontSharedMemory = 4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                       IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] bool IsFontLoaded(SharedFontType type) const noexcept;
    [[nodiscard]] u32 GetFontSize(SharedFontType type) const noexcept;

private:
    u32 HandleRequestSharedFont(const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetSharedFontLoadState(const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetSharedFontSize(const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetSharedFontAddress(const IpcRequestReader& request, IpcReplyWriter& reply);
    u32 HandleGetSharedFontSharedMemory(const IpcContext& ctx, IpcReplyWriter& reply);

    bool EnsureSharedMemoryCreated(const IpcContext& ctx);

    bool shared_created_{false};
    vaddr_t shared_addr_{0};
};

} // namespace nemu::core::kernel::ipc
