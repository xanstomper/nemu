#include "set_u_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

SetUserService::SetUserService()
    : IIpcService("set:u") {}

namespace {
/// FirmwareVersion struct layout (matches libnx SetSysFirmwareVersion):
/// u8 major, minor, micro, revision; u8 kind[3]; u8 is_updatable;
/// char sysversion[16]; char displayversion[9]; char display_title[9].
void WriteFirmwareVersion(IpcReplyWriter& reply, const SetUserService::Settings& s) {
    reply.Payload<u8>(0, s.major);
    reply.Payload<u8>(1, s.minor);
    reply.Payload<u8>(2, s.micro);
    reply.Payload<u8>(3, s.revision);
    reply.Payload<u8>(4, 1); // kind[0] (Dev)
    reply.Payload<u8>(5, 0); // kind[1]
    reply.Payload<u8>(6, 0); // kind[2]
    reply.Payload<u8>(7, 1); // is_updatable
    char fw[16] = {0};
    std::snprintf(fw, sizeof(fw), "%u.%u.%u", s.major, s.minor, s.micro);
    (void)reply.WriteString(8, fw);   // sysversion
    (void)reply.WriteString(24, fw);  // displayversion
    (void)reply.WriteString(33, fw);  // display_title
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 0x100);
}
} // namespace

u32 SetUserService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                  IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case GetFirmwareVersion:
            WriteFirmwareVersion(reply, settings_);
            return static_cast<u32>(IpcResult::Success);
        case GetLanguageCode:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
            reply.Payload<u32>(0, settings_.language_code);
            return static_cast<u32>(IpcResult::Success);
        case GetRegionCode:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
            reply.Payload<u32>(0, settings_.region_code);
            return static_cast<u32>(IpcResult::Success);
        case GetAvailableLanguageCodes: {
            // Single language ("en" => 0x656E) followed by a count of 1.
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 2 * sizeof(u32));
            reply.Payload<u32>(0, settings_.language_code);
            reply.Payload<u32>(4, 1);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("set:u", "Unhandled set:u command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc