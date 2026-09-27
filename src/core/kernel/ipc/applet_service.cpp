#include "applet_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// CommonStateGetterService (applet:ICommonStateGetter)
// ---------------------------------------------------------------------------

CommonStateGetterService::CommonStateGetterService()
    : IIpcService("applet:ICommonStateGetter") {
    message_event_ = std::make_shared<KEvent>(true); // auto-reset
    message_event_->Signal();
}

u32 CommonStateGetterService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case GetEventObserver: {
            Handle event_handle = 0;
            if (ctx.handle_table && message_event_) {
                event_handle = ctx.handle_table->CreateHandle(message_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, event_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case ReceiveMessage: {
            // Message 0x0F: FocusStateChanged (tells the game it is in focus and ready)
            reply.Begin(0, 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, 0x0F);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetOperationMode: {
            // 0 = Handheld (720p), 1 = Docked (1080p/TV mode)
            const u8 mode = docked_ ? 1 : 0;
            reply.Begin(0, 5);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u8>(4, mode);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetPerformanceMode: {
            // 0 = Normal/Handheld, 1 = Boost/Docked
            const u32 perf = docked_ ? 1 : 0;
            reply.Begin(0, 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, perf);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetCradleFirmwareVersion: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u32>(4, 0x00010000); // v1.0.0
            return static_cast<u32>(IpcResult::Success);
        }

        case GetCurrentFocusState: {
            // 1 = InFocus
            reply.Begin(0, 5);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            reply.Payload<u8>(4, 1);
            return static_cast<u32>(IpcResult::Success);
        }

        case SetFocusHandlingMode:
        case SetRestartMessageEnabled:
        case SetScreenShotPermission:
        case SetAlbumImageOrientation: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("applet", "ICommonStateGetter: Stubbing command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
            return static_cast<u32>(IpcResult::Success);
    }
}

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
            // Real reply: b8 (one byte) — verified against Ryujinx
            // IApplicationFunctions.cs cmd 40 (NotifyRunning() -> b8).
            reply.Begin(0, 5);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 1); // out_running = true
            return static_cast<u32>(IpcResult::Success);
        }

        case GetDesiredLanguage: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 0); // English (0)
            return static_cast<u32>(IpcResult::Success);
        }

        case SetTerminateResult: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
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

        case GetPseudoDeviceId: {
            reply.Begin(0, 20);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 0x0123456789ABCDEFULL);
            reply.Payload<u64>(12, 0xFEDCBA9876543210ULL);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_DEBUG("applet", "IApplicationFunctions: stubbing command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
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
// SelfControllerService
// ---------------------------------------------------------------------------

SelfControllerService::SelfControllerService()
    : IIpcService("applet:ISelfController") {
    launchable_event_ = std::make_shared<KEvent>(true);
    launchable_event_->Signal();
}

u32 SelfControllerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;
    switch (x_id) {
        case GetLibraryAppletLaunchableEvent: {
            Handle event_h = 0;
            if (ctx.handle_table && launchable_event_) {
                event_h = ctx.handle_table->CreateHandle(launchable_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, event_h);
            return static_cast<u32>(IpcResult::Success);
        }
        case CreateManagedDisplayLayer: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 1); // LayerId = 1
            return static_cast<u32>(IpcResult::Success);
        }
        case IsSystemBufferSharingEnabled: {
            reply.Begin(0, 5);
            reply.Payload<u32>(0, 0);
            reply.Payload<u8>(4, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case GetTotalMemoryAllocated: {
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case SetFocusHandlingMode:
        case SetRestartMessageEnabled:
        case SetScreenShotPermission:
        case SetOperationModeChangedNotification:
        case SetPerformanceModeChangedNotification:
        case SetScreenShotImageOrientation:
        case SetAlbumImageOrientation:
        case SetIdleTimeDetectionExtension:
        case SetMediaPlaybackState:
        case EnterFatalSection:
        case LeaveFatalSection:
        case LockExit:
        case UnlockExit:
        case Exit:
        default: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
    }
}

// ---------------------------------------------------------------------------
// AudioControllerService
// ---------------------------------------------------------------------------

AudioControllerService::AudioControllerService()
    : IIpcService("applet:IAudioController") {}

u32 AudioControllerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;
    (void)x_id;
    reply.Begin(0, 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

// ---------------------------------------------------------------------------
// DisplayControllerService
// ---------------------------------------------------------------------------

DisplayControllerService::DisplayControllerService()
    : IIpcService("applet:IDisplayController") {}

u32 DisplayControllerService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;
    (void)x_id;
    reply.Begin(0, 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
}

// ---------------------------------------------------------------------------
// LibraryAppletCreatorService
// ---------------------------------------------------------------------------

LibraryAppletCreatorService::LibraryAppletCreatorService()
    : IIpcService("applet:ILibraryAppletCreator") {}

u32 LibraryAppletCreatorService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;
    (void)x_id;
    reply.Begin(0, 4);
    reply.Payload<u32>(0, 0);
    return static_cast<u32>(IpcResult::Success);
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
        case OpenCommonStateGetter: {
            auto common = std::make_shared<CommonStateGetterService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(common);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenSelfController: {
            auto self_ctrl = std::make_shared<SelfControllerService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(self_ctrl);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenWindowController:
        case OpenWindowControllerLegacy: {
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

        case OpenAudioController: {
            auto audio_ctrl = std::make_shared<AudioControllerService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(audio_ctrl);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenDisplayController: {
            auto disp_ctrl = std::make_shared<DisplayControllerService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(disp_ctrl);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenLibraryAppletCreator: {
            auto creator = std::make_shared<LibraryAppletCreatorService>();
            auto session = std::make_shared<KClientSession>();
            session->SetService(creator);

            Handle h = 0;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }

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
