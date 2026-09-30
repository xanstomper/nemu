#pragma once

#include "ipc_service.hpp"
#include "core/kernel/k_event.hpp"
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <atomic>

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

    void SetDockedMode(bool docked) noexcept { s_global_docked_mode_.store(docked); }
    [[nodiscard]] bool IsDockedMode() const noexcept { return s_global_docked_mode_.load(); }

    static void SetGlobalDockedMode(bool docked) noexcept { s_global_docked_mode_.store(docked); }
    [[nodiscard]] static bool IsGlobalDockedMode() noexcept { return s_global_docked_mode_.load(); }

private:
    std::shared_ptr<KEvent> message_event_;
    static inline std::atomic<bool> s_global_docked_mode_{true};
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

class StorageService;

class StorageAccessorService final : public IIpcService {
public:
    explicit StorageAccessorService(std::shared_ptr<StorageService> storage);
    ~StorageAccessorService() override = default;

    enum : u32 {
        GetSize = 0x0,
        Write = 0xA,    // 10
        Read = 0xB,     // 11
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<StorageService> storage_;
};

class StorageService final : public IIpcService, public std::enable_shared_from_this<StorageService> {
public:
    explicit StorageService(size_t size = 0);
    explicit StorageService(std::vector<u8> data);
    ~StorageService() override = default;

    enum : u32 {
        Open = 0x0,
        OpenTransferStorage = 0x1,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::vector<u8>& GetData() noexcept { return data_; }
    [[nodiscard]] const std::vector<u8>& GetData() const noexcept { return data_; }

private:
    std::vector<u8> data_;
};

class LibraryAppletAccessorService final : public IIpcService {
public:
    explicit LibraryAppletAccessorService(u32 applet_id, u32 applet_mode = 0);
    ~LibraryAppletAccessorService() override = default;

    enum : u32 {
        GetAppletStateChangedEvent = 0x0,
        IsCompleted = 0x1,
        Start = 0xA,                          // 10
        RequestExit = 0x14,                   // 20
        Terminate = 0x19,                     // 25
        GetResult = 0x1E,                     // 30
        PushInData = 0x64,                    // 100
        PopOutData = 0x65,                    // 101
        PushInteractiveInData = 0x67,         // 103
        PopInteractiveOutData = 0x68,         // 104
        GetPopOutDataEvent = 0x69,            // 105
        GetPopInteractiveOutDataEvent = 0x6A, // 106
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    void PushOutputData(std::shared_ptr<StorageService> storage);

private:
    u32 applet_id_{0};
    u32 applet_mode_{0};
    bool is_completed_{false};
    std::shared_ptr<KEvent> state_changed_event_;
    std::shared_ptr<KEvent> pop_out_data_event_;
    std::deque<std::shared_ptr<StorageService>> in_queue_;
    std::deque<std::shared_ptr<StorageService>> out_queue_;
};

class LibraryAppletCreatorService final : public IIpcService {
public:
    LibraryAppletCreatorService();
    ~LibraryAppletCreatorService() override = default;

    enum : u32 {
        CreateLibraryApplet = 0x0,
        CreateLibraryAppletEx = 0x3,
        CreateStorage = 0xA,                  // 10
        CreateTransferMemoryStorage = 0xB,    // 11
        CreateHandleStorage = 0xC,            // 12
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
