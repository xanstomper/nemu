#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel::ipc {

class CommonStateGetterService final : public IIpcService {
public:
    CommonStateGetterService();
    ~CommonStateGetterService() override = default;

    enum : u32 {
        GetEventObserver = 0,
        ReceiveMessage = 1,
        GetOperationMode = 5,
        GetPerformanceMode = 6,
        GetCradleFirmwareVersion = 8,
        GetCurrentFocusState = 9,
        SetFocusHandlingMode = 10,
        SetRestartMessageEnabled = 11,
        SetScreenShotPermission = 12,
        SetAlbumImageOrientation = 14,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    void SetDockedMode(bool docked) noexcept { docked_ = docked; }
    [[nodiscard]] bool IsDockedMode() const noexcept { return docked_; }

private:
    std::shared_ptr<KEvent> message_event_;
    bool docked_{true}; // Xbox console defaults to Docked performance mode
};

class ApplicationFunctionsService final : public IIpcService {
public:
    ApplicationFunctionsService();
    ~ApplicationFunctionsService() override = default;

    enum : u32 {
        InitializeApplicationCopyrightAndCrashReportGlobals = 0x1,
        NotifyRunning = 0x14,        // 20
        GetDesiredLanguage = 0x15,   // 21
        SetTerminateResult = 0x16,   // 22
        GetDisplayVersion = 0x17,    // 23
        EnsureSaveData = 0x28,       // 40
        GetPseudoDeviceId = 0x32,    // 50
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class WindowControllerService final : public IIpcService {
public:
    WindowControllerService();
    ~WindowControllerService() override = default;

    enum : u32 {
        GetAppletResourceUserId = 0x1,
        AcquireForegroundRights = 0xA, // 10
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class AppletSessionService final : public IIpcService {
public:
    AppletSessionService();
    ~AppletSessionService() override = default;

    enum : u32 {
        OpenCommonStateGetter = 0x0,
        OpenApplicationFunctions = 0x14, // 20
        OpenWindowController = 0x15,     // 21
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class AppletManagerService final : public IIpcService {
public:
    explicit AppletManagerService(std::string name = "appletOE");
    ~AppletManagerService() override = default;

    enum : u32 {
        OpenSession = 0x0,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
