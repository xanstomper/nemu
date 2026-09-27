#pragma once

#include "ipc_service.hpp"
#include <string>
#include <vector>

namespace nemu::core::kernel::ipc {

/// Album / Screen Capture Service (caps:u, caps:a, caps:c, caps:ss, caps:su, caps:sc)
/// Ported from Nintendo Switch Horizon OS / Eden emulator specifications.
class CapsService final : public IIpcService {
public:
    explicit CapsService(std::string name = "caps:u");
    ~CapsService() override = default;

    enum Commands : u32 {
        SetShimLibraryVersion = 32,
        GetAlbumFileList0AafeAruidDeprecated = 102,
        DeleteAlbumFileByAruid = 103,
        GetAlbumFileSizeByAruid = 104,
        DeleteAlbumFileByAruidForDebug = 105,
        LoadAlbumScreenShotImageByAruid = 110,
        LoadAlbumScreenShotThumbnailImageByAruid = 120,
        PrecheckToCreateContentsByAruid = 130,
        GetAlbumFileList1AafeAruidDeprecated = 140,
        GetAlbumFileList2AafeUidAruidDeprecated = 141,
        GetAlbumFileList3AaeAruid = 142,
        GetAlbumFileList4AaeUidAruid = 143,
        GetAllAlbumFileList3AaeAruid = 144,
        CaptureScreenShot = 201,
        CaptureScreenShotWithUserData = 202,
        OpenAccessorSessionForApplication = 60002,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] u64 GetShimLibraryVersion() const noexcept { return shim_library_version_; }

private:
    u64 shim_library_version_{0};
};

/// Album Accessor Session (subservice returned by OpenAccessorSessionForApplication)
class CapsAccessorSessionService final : public IIpcService {
public:
    explicit CapsAccessorSessionService(std::string name = "caps:u:accessor");
    ~CapsAccessorSessionService() override = default;

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;
};

} // namespace nemu::core::kernel::ipc
