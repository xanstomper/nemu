#include "applet_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

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
// StorageAccessorService
// ---------------------------------------------------------------------------

StorageAccessorService::StorageAccessorService(std::shared_ptr<StorageService> storage)
    : IIpcService("applet:IStorageAccessor"), storage_(std::move(storage)) {}

u32 StorageAccessorService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    switch (x_id) {
        case GetSize: {
            const u64 sz = storage_ ? static_cast<u64>(storage_->GetData().size()) : 0;
            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0); // Success
            reply.Payload<u64>(4, sz);
            return static_cast<u32>(IpcResult::Success);
        }
        case Write: {
            const size_t offset = static_cast<size_t>(request.Payload<u64>(0));
            if (storage_) {
                for (const auto& desc : request.GetBufferDescriptors()) {
                    if ((desc.type == IpcBufferType::A_Send || desc.type == IpcBufferType::X_Pointer) &&
                        ctx.memory && desc.address != 0 && desc.size > 0) {
                        auto& data = storage_->GetData();
                        const size_t end_pos = offset + desc.size;
                        if (end_pos > data.size()) {
                            data.resize(end_pos, 0);
                        }
                        (void)ctx.memory->ReadBlock(desc.address, data.data() + offset, desc.size);
                        break;
                    }
                }
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case Read: {
            const size_t offset = static_cast<size_t>(request.Payload<u64>(0));
            if (storage_) {
                for (const auto& desc : request.GetBufferDescriptors()) {
                    if ((desc.type == IpcBufferType::B_Receive || desc.type == IpcBufferType::C_Receive) &&
                        ctx.memory && desc.address != 0 && desc.size > 0) {
                        const auto& data = storage_->GetData();
                        if (offset < data.size()) {
                            const size_t copy_sz = std::min(desc.size, data.size() - offset);
                            (void)ctx.memory->WriteBlock(desc.address, data.data() + offset, copy_sz);
                        }
                        break;
                    }
                }
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("applet", "IStorageAccessor: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// StorageService
// ---------------------------------------------------------------------------

StorageService::StorageService(size_t size)
    : IIpcService("applet:IStorage"), data_(size, 0) {}

StorageService::StorageService(std::vector<u8> data)
    : IIpcService("applet:IStorage"), data_(std::move(data)) {}

u32 StorageService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;
    switch (x_id) {
        case Open:
        case OpenTransferStorage: {
            auto accessor = std::make_shared<StorageAccessorService>(shared_from_this());
            auto session = std::make_shared<KClientSession>();
            session->SetService(accessor);

            Handle h = InvalidHandle;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("applet", "IStorage: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// LibraryAppletAccessorService
// ---------------------------------------------------------------------------

LibraryAppletAccessorService::LibraryAppletAccessorService(u32 applet_id, u32 applet_mode)
    : IIpcService("applet:ILibraryAppletAccessor"),
      applet_id_(applet_id),
      applet_mode_(applet_mode),
      state_changed_event_(std::make_shared<KEvent>(false)),
      pop_out_data_event_(std::make_shared<KEvent>(false)) {}

void LibraryAppletAccessorService::PushOutputData(std::shared_ptr<StorageService> storage) {
    out_queue_.push_back(std::move(storage));
    pop_out_data_event_->Signal();
}

u32 LibraryAppletAccessorService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    switch (x_id) {
        case GetAppletStateChangedEvent: {
            Handle h = InvalidHandle;
            if (ctx.handle_table && state_changed_event_) {
                h = ctx.handle_table->CreateHandle(state_changed_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        case IsCompleted: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, is_completed_ ? 1 : 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case Start: {
            is_completed_ = true;

            // Generate synthetic completion data if none already staged
            if (out_queue_.empty()) {
                if (applet_id_ == 0x08) {
                    // Software Keyboard (swkbd):
                    // Output header: u64 result = 0 (Submit / OK)
                    // Followed by null-terminated UTF-8 string "Player"
                    std::vector<u8> swkbd_resp(512, 0);
                    const u64 ok_res = 0;
                    std::memcpy(swkbd_resp.data(), &ok_res, sizeof(ok_res));
                    const char default_name[] = "Player";
                    std::memcpy(swkbd_resp.data() + 8, default_name, sizeof(default_name));
                    out_queue_.push_back(std::make_shared<StorageService>(std::move(swkbd_resp)));
                } else if (applet_id_ == 0x10) {
                    // ProfileSelect applet:
                    // Return result = 0, default user profile UUID
                    std::vector<u8> profile_resp(32, 0);
                    const u32 ok_res = 0;
                    std::memcpy(profile_resp.data(), &ok_res, sizeof(ok_res));
                    const u64 uid_low = 1;
                    const u64 uid_high = 2;
                    std::memcpy(profile_resp.data() + 8, &uid_low, sizeof(uid_low));
                    std::memcpy(profile_resp.data() + 16, &uid_high, sizeof(uid_high));
                    out_queue_.push_back(std::make_shared<StorageService>(std::move(profile_resp)));
                } else {
                    // Generic success response
                    std::vector<u8> generic_resp(32, 0);
                    out_queue_.push_back(std::make_shared<StorageService>(std::move(generic_resp)));
                }
            }

            state_changed_event_->Signal();
            pop_out_data_event_->Signal();

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case RequestExit:
        case Terminate: {
            is_completed_ = true;
            state_changed_event_->Signal();
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case GetResult: {
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, 0); // ErrorCode 0
            return static_cast<u32>(IpcResult::Success);
        }
        case PushInData:
        case PushInteractiveInData: {
            const Handle storage_handle = request.Payload<Handle>(0);
            if (ctx.handle_table && storage_handle != InvalidHandle) {
                auto session = std::dynamic_pointer_cast<KClientSession>(ctx.handle_table->GetObject(storage_handle));
                if (session) {
                    auto storage = std::dynamic_pointer_cast<StorageService>(session->GetService());
                    if (storage) {
                        in_queue_.push_back(storage);
                    }
                }
            }
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case PopOutData:
        case PopInteractiveOutData: {
            std::shared_ptr<StorageService> storage;
            if (!out_queue_.empty()) {
                storage = out_queue_.front();
                out_queue_.pop_front();
            } else {
                storage = std::make_shared<StorageService>(32);
            }

            auto session = std::make_shared<KClientSession>();
            session->SetService(storage);

            Handle h = InvalidHandle;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        case GetPopOutDataEvent:
        case GetPopInteractiveOutDataEvent: {
            Handle h = InvalidHandle;
            if (ctx.handle_table && pop_out_data_event_) {
                h = ctx.handle_table->CreateHandle(pop_out_data_event_);
            }
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("applet", "ILibraryAppletAccessor: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
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
    switch (x_id) {
        case CreateLibraryApplet:
        case CreateLibraryAppletEx: {
            const u32 applet_id = request.Payload<u32>(0);
            const u32 applet_mode = request.Payload<u32>(4);
            NEMU_LOG_INFO("applet", "ILibraryAppletCreator: CreateLibraryApplet id=0x{:X}, mode={}", applet_id, applet_mode);

            auto accessor = std::make_shared<LibraryAppletAccessorService>(applet_id, applet_mode);
            auto session = std::make_shared<KClientSession>();
            session->SetService(accessor);

            Handle h = InvalidHandle;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        case CreateStorage: {
            const u64 size = request.Payload<u64>(0);
            const size_t byte_size = static_cast<size_t>(size);
            NEMU_LOG_INFO("applet", "ILibraryAppletCreator: CreateStorage size={}", byte_size);

            auto storage = std::make_shared<StorageService>(byte_size);
            auto session = std::make_shared<KClientSession>();
            session->SetService(storage);

            Handle h = InvalidHandle;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        case CreateTransferMemoryStorage:
        case CreateHandleStorage: {
            auto storage = std::make_shared<StorageService>(0);
            auto session = std::make_shared<KClientSession>();
            session->SetService(storage);

            Handle h = InvalidHandle;
            if (ctx.handle_table) {
                h = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, h);
            return static_cast<u32>(IpcResult::Success);
        }
        default:
            NEMU_LOG_WARN("applet", "ILibraryAppletCreator: Unhandled command 0x{:X}", x_id);
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
