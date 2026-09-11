#pragma once

#include "ipc_service.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel::ipc {

class ApplicationFunctionsService final : public IIpcService {
public:
    ApplicationFunctionsService();
    ~ApplicationFunctionsService() override = default;

    enum : u32 {
        InitializeApplicationCopyrightAndCrashReportGlobals = 0x1,
        NotifyRunning = 0x14,        // 20
        GetDesiredLanguage = 0x15,   // 21
        GetDisplayVersion = 0x16,    // 22
        EnsureSaveData = 0x28,       // 40
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
