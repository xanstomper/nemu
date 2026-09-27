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

class SelfControllerService final : public IIpcService {
public:
    SelfControllerService();
    ~SelfControllerService() override = default;

    enum : u32 {
        Exit = 0x0,
        LockExit = 0x1,
        UnlockExit = 0x2,
        EnterFatalSection = 0x9,
        LeaveFatalSection = 0xA,
        GetLibraryAppletLaunchableEvent = 0xB, // 11
        SetScreenShotPermission = 0xC,         // 12
        SetOperationModeChangedNotification = 0xD,
        SetPerformanceModeChangedNotification = 0xE,
        SetFocusHandlingMode = 0xF,           // 15
        SetRestartMessageEnabled = 0x10,       // 16
        SetScreenShotImageOrientation = 0x11,
        CreateManagedDisplayLayer = 0x28,     // 40
        IsSystemBufferSharingEnabled = 0x29,   // 41
        GetTotalMemoryAllocated = 0x2A,       // 42
        SetAlbumImageOrientation = 0x2C,      // 44
        SetIdleTimeDetectionExtension = 0x3C, // 60
        SetMediaPlaybackState = 0x3E,         // 62
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<KEvent> launchable_event_;
};

class AudioControllerService final : public IIpcService {
public:
    AudioControllerService();
    ~AudioControllerService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class DisplayControllerService final : public IIpcService {
public:
    DisplayControllerService();
    ~DisplayControllerService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class LibraryAppletCreatorService final : public IIpcService {
public:
    LibraryAppletCreatorService();
    ~LibraryAppletCreatorService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

class AppletSessionService final : public IIpcService {
public:
    AppletSessionService();
    ~AppletSessionService() override = default;

    enum : u32 {
        OpenCommonStateGetter = 0x0,
        OpenSelfController = 0x1,
        OpenWindowController = 0x2,
        OpenAudioController = 0x3,
        OpenDisplayController = 0x4,
        OpenLibraryAppletCreator = 0xB,    // 11
        OpenApplicationFunctions = 0x14,  // 20
        OpenWindowControllerLegacy = 0x15,// 21
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
