#pragma once

#include "ipc_service.hpp"

namespace nemu::core::kernel::ipc {

/// set:sys service. System settings HLE: firmware version, console id, color
/// set id, and language code queries. Static values model a generic dev-kit
/// console and are recorded in one struct for testability.
class SetSysService final : public IIpcService {
public:
    explicit SetSysService(std::string name = "set:sys");

    struct Settings {
        u8 major{1};
        u8 minor{0};
        u8 micro{0};
        u8 revision{0};
        u32 color_set{2};          // 2 == Standard
        u32 language_code{0x656E}; // "en"
        char firmware_version_str[16]{"1.0.0"};
        char release_notes[0x8C]{0};
    };

    enum : u32 {
        GetFirmwareVersion = 0x0,
        GetConsoleId = 0x3,
        GetColorSetId = 0x4,
        GetLanguageCode = 0x12,
    };

    [[nodiscard]] const Settings& GetSettings() const noexcept { return settings_; }
    Settings& EditSettings() noexcept { return settings_; }

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    Settings settings_;
};

} // namespace nemu::core::kernel::ipc