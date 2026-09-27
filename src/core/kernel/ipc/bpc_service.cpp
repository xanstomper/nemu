#include "bpc_service.hpp"
#include "platform/logger.hpp"
#include "core/kernel/k_handle_table.hpp"
#include <chrono>

namespace nemu::core::kernel::ipc {

BpcService::BpcService(std::string name)
    : IIpcService(std::move(name)),
      power_event_(std::make_shared<KEvent>(/*auto_clear=*/false)) {}

u32 BpcService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 x_id) {
    const std::string& svc_name = GetName();

    if (svc_name == "bpc:r") {
        switch (x_id) {
            case GetRtcTime: {
                const auto now = std::chrono::system_clock::now();
                const auto epoch_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
                reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
                reply.Payload<u32>(0, 0); // Result Success
                reply.Payload<s64>(8, epoch_sec);
                return static_cast<u32>(IpcResult::Success);
            }
            case SetRtcTime:
            case ClearRtcResetDetected:
            case SetUpRtcResetOnShutdown: {
                reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
                reply.Payload<u32>(0, 0);
                return static_cast<u32>(IpcResult::Success);
            }
            case GetRtcResetDetected: {
                reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
                reply.Payload<u32>(0, 0); // Result Success
                reply.Payload<u8>(4, 0);  // reset_detected = false
                return static_cast<u32>(IpcResult::Success);
            }
            default:
                break;
        }
    } else if (svc_name == "bpc:ams") {
        switch (x_id) {
            case RebootToFatalError:
                NEMU_LOG_WARN("bpc:ams", "RebootToFatalError invoked by guest title");
                reboot_requested_ = true;
                reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
                reply.Payload<u32>(0, 0);
                return static_cast<u32>(IpcResult::Success);

            case SetRebootPayload:
                reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
                reply.Payload<u32>(0, 0);
                return static_cast<u32>(IpcResult::Success);

            default:
                break;
        }
    }

    // Default bpc / bpc:c / bpc:b / bpc:w commands
    switch (x_id) {
        case ShutdownSystem:
            NEMU_LOG_INFO("bpc", "ShutdownSystem invoked by guest title");
            shutdown_requested_ = true;
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);

        case RebootSystem:
            NEMU_LOG_INFO("bpc", "RebootSystem invoked by guest title");
            reboot_requested_ = true;
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);

        case GetWakeupReason: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // WakeupReason: PowerButton
            return static_cast<u32>(IpcResult::Success);
        }

        case GetShutdownReason: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // ShutdownReason: Normal
            return static_cast<u32>(IpcResult::Success);
        }

        case GetAcOk: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u8>(4, 1);  // ac_ok = true (100% plugged-in)
            return static_cast<u32>(IpcResult::Success);
        }

        case GetBoardPowerControlEvent:
        case GetPowerEvent: {
            Handle event_handle = 0;
            if (ctx.handle_table) {
                event_handle = ctx.handle_table->CreateHandle(power_event_);
            }
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSleepButtonState: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 0); // sleep button not pressed
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            (void)request;
            NEMU_LOG_DEBUG("bpc", "{}: Handled command 0x{:X}", svc_name, x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 16);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
