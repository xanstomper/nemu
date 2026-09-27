#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <string>
#include <vector>
#include <memory>

namespace nemu::core::kernel::ipc {

/// aoc:u / aoc:s - Add-On Content (DLC) Manager Service.
/// Allows commercial games to query installed DLC packages and contents.
class AocService final : public IIpcService {
public:
    explicit AocService(std::string name = "aoc:u");
    ~AocService() override = default;

    enum Commands : u32 {
        CountAddOnContentByApplicationId = 0,
        ListAddOnContentByApplicationId = 1,
        CountAddOnContent = 2,
        ListAddOnContent = 3,
        GetAddOnContentBaseIdByApplicationId = 4,
        GetAddOnContentBaseId = 5,
        PrepareAddOnContentByApplicationId = 6,
        PrepareAddOnContent = 7,
        GetAddOnContentListChangedEvent = 8,
        GetAddOnContentLostErrorCode = 9,
        GetAddOnContentListChangedEventWithProcessId = 10,
        NotifyMountAddOnContent = 11,
        NotifyUnmountAddOnContent = 12,
        CheckAddOnContentMountStatus = 50,
        CreateEcPurchasedEventManager = 100,
        CreatePermanentEcPurchasedEventManager = 101,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    void SetDlcCount(u32 count) noexcept { dlc_count_ = count; }
    [[nodiscard]] u32 GetDlcCount() const noexcept { return dlc_count_; }

private:
    std::shared_ptr<KEvent> change_event_;
    u32 dlc_count_{0};
};

} // namespace nemu::core::kernel::ipc
