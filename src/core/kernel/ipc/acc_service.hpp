#pragma once

#include "ipc_service.hpp"
#include <array>
#include <string>

namespace nemu::core::kernel::ipc {

struct UserId {
    u64 low{1};
    u64 high{0};
};

class ProfileService final : public IIpcService {
public:
    explicit ProfileService(UserId user_id, std::string nickname = "Xbox Player");
    ~ProfileService() override = default;

    enum : u32 {
        Get = 0x0,
        GetBase = 0x1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    UserId user_id_{};
    std::string nickname_;
};

class AccountService final : public IIpcService {
public:
    AccountService();
    ~AccountService() override = default;

    enum : u32 {
        GetUserCount = 0x0,
        GetUserExistence = 0x1,
        ListOpenUsers = 0x2,
        GetLastOpenedUser = 0x3,
        GetProfile = 0x4,
        InitializeApplicationInfo = 0x64, // 100
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] UserId GetDefaultUser() const noexcept { return default_user_; }

private:
    UserId default_user_{1, 0};
};

} // namespace nemu::core::kernel::ipc
