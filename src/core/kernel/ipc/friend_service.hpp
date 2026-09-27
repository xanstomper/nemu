#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel::ipc {

/// friend:u, friend:v - Friend and Social service interface.
class FriendService final : public IIpcService {
public:
    explicit FriendService(std::string name = "friend:u");
    ~FriendService() override = default;

    enum Commands : u32 {
        CreateFriendService = 0,
        CreateNotificationService = 1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

/// IFriendService subservice returned by CreateFriendService
class FriendSubService final : public IIpcService {
public:
    explicit FriendSubService(std::string name = "friend:IFriendService");
    ~FriendSubService() override = default;

    enum Commands : u32 {
        GetCompletionEvent = 0,
        Cancel = 1,
        GetFriendList = 10101,
        CheckFriendListAvailability = 10120,
        DeclareCloseOnlinePlaySession = 10601,
        UpdateUserPresence = 10610,
        GetFriendCount = 20100,
        GetNewlyFriendCount = 20101,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<KEvent> completion_event_;
};

} // namespace nemu::core::kernel::ipc
