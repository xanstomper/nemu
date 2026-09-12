#pragma once

#include "ipc_service.hpp"

namespace nemu::core::kernel::ipc {

/// set:u service. System-Settings user-facing interface that almost every
/// title connects to immediately after sm:. Mirrors the real set:u command
/// set (firmware version, language, region) backed by static generic dev-kit
/// values shared with SetSysService.
class SetUserService final : public IIpcService {
public:
    SetUserService();

    struct Settings {
        u8 major{1};
        u8 minor{0};
        u8 micro{0};
        u8 revision{0};
        u32 language_code{0x656E}; // "en"
        u8 region_code{2};         // 0=JP,1=US,2=EU
    };

    enum : u32 {
        GetFirmwareVersion = 0x0,
        GetLanguageCode = 0x1,
        GetRegionCode = 0x2,
        GetAvailableLanguageCodes = 0xB,
    };

    [[nodiscard]] const Settings& GetSettings() const noexcept { return settings_; }
    Settings& EditSettings() noexcept { return settings_; }

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    Settings settings_;
};

} // namespace nemu::core::kernel::ipc