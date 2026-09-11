#include "applet_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// ApplicationFunctionsService
// ---------------------------------------------------------------------------

ApplicationFunctionsService::ApplicationFunctionsService()
    : IIpcService("applet:IApplicationFunctions") {}

u32 ApplicationFunctionsService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case InitializeApplicationCopyrightAndCrashReportGlobals: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case NotifyRunning: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, 1); // out_running = true
            return static_cast<u32>(IpcResult::Success);
        }

        case GetDesiredLanguage: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 0); // English (0)
            return static_cast<u32>(IpcResult::Success);
        }

        case GetDisplayVersion: {
            reply.Begin(0, 24);
            reply.Payload<u32>(0, 0);
            (void)reply.WriteString(4, "1.0.0");
            return static_cast<u32>(IpcResult::Success);
        }

        case EnsureSaveData: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 0); // 0 bytes required
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("applet", "IApplicationFunctions: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

// ---------------------------------------------------------------------------
// WindowControllerService
// ---------------------------------------------------------------------------

WindowControllerService::WindowControllerService()
    : IIpcService("applet:IWindowController") {}

u32 WindowControllerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case GetAppletResourceUserId: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 1); // Resource User Id = 1
            return static_cast<u32>(IpcResult::Success);
        }

        case AcquireForegroundRights: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AppletSessionService
// ---------------------------------------------------------------------------

AppletSessionService::AppletSessionService()
    : IIpcService("applet:Session") {}

u32 AppletSessionService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case OpenApplicationFunctions: {
            auto funcs = std::make_shared<ApplicationFunctionsService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(funcs);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenWindowController: {
            auto win = std::make_shared<WindowControllerService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(win);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// AppletManagerService
// ---------------------------------------------------------------------------

AppletManagerService::AppletManagerService(std::string name)
    : IIpcService(std::move(name)) {}

u32 AppletManagerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case OpenSession: {
            auto applet_sess = std::make_shared<AppletSessionService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(applet_sess);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("applet", "{}: Unhandled command 0x{:X}", GetName(), x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
