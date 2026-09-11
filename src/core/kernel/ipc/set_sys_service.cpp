#include "set_sys_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

namespace {
    /// FirmwareVersion struct layout (matches libnx SetSysFirmwareVersion):
    /// u8 major, minor, micro, revision; u8 kind[3]; u8 is_updatable;
    /// char sysversion[16]; char displayversion[9]; char display_title[9];
    void WriteFirmwareVersion(IpcReplyWriter& reply, const SetSysService::Settings& s) {
        reply.Payload<u8>(0, s.major);
        reply.Payload<u8>(1, s.minor);
        reply.Payload<u8>(2, s.micro);
        reply.Payload<u8>(3, s.revision);
        reply.Payload<u8>(4, 1); // kind[0] (Dev)
        reply.Payload<u8>(5, 0); // kind[1]
        reply.Payload<u8>(6, 0); // kind[2]
        reply.Payload<u8>(7, 1); // is_updatable
        (void)reply.WriteString(8, s.firmware_version_str);  // sysversion
        (void)reply.WriteString(24, s.firmware_version_str); // displayversion
        (void)reply.WriteString(33, s.firmware_version_str); // display_title
        reply.Begin(static_cast<u32>(IpcCommandType::Request), 0x100);
    }
} // namespace

SetSysService::SetSysService()
    : IIpcService("set:sys") {}

u32 SetSysService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                                 IpcReplyWriter& reply, u32 x_id) {
    (void)ctx;
    (void)request;
    switch (x_id) {
        case GetFirmwareVersion:
            WriteFirmwareVersion(reply, settings_);
            return static_cast<u32>(IpcResult::Success);
        case GetConsoleId: {
            // 0x10-byte console id: two u64 words of a generic breadcrumb.
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0x10);
            reply.Payload<u64>(0, 0x4E454D55434F4E31ULL); // "NEMUCON1"
            reply.Payload<u64>(8, 0x44455600ULL);         // "DEV\0"
            return static_cast<u32>(IpcResult::Success);
        }
        case GetColorSetId:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
            reply.Payload<u32>(0, settings_.color_set);
            return static_cast<u32>(IpcResult::Success);
        case GetLanguageCode:
            reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
            reply.Payload<u32>(0, settings_.language_code);
            return static_cast<u32>(IpcResult::Success);
        default:
            NEMU_LOG_WARN("set:sys", "Unhandled set:sys command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc