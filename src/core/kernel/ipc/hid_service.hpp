#pragma once

#include "ipc_service.hpp"
#include "hid_shared.hpp"
#include "k_shared_memory.hpp"

namespace nemu::core::memory {
class VirtualMemory;
} // namespace nemu::core::memory

namespace nemu::core::kernel::ipc {

/// IAppletResource: the per-session object hid's CreateAppletResource returns.
/// Real protocol: its GetSharedMemoryHandle (cmd 0) returns the shared memory
/// handle the guest maps to read pad state. Delegates to the owning HidService
/// so shared-memory materialisation stays in one place.
/// Implementation (constructor + handlers) lives in hid_service.cpp.
class HidService; // fwd

class HidAppletResourceService final : public IIpcService {
public:
    explicit HidAppletResourceService(HidService* owner);

    enum : u32 {
        GetSharedMemoryHandle = 0x0,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    HidService* owner_;
};

/// hid: service. HLE for the Switch HID system service. It maps a shared-memory
/// page into the guest and exposes module-scoped HLE update commands so the
/// emulator frontend (or tests) can inject button / stick state that guest
/// input code reads from the shared pad entries.
///
/// Real guest protocol (what libnx homebrew and retail games speak):
///   hidInitialize (cmd 0)                      -> no data
///   hidCreateAppletResource (cmd 0)            -> IAppletResource session handle
///   [IAppletResource] GetSharedMemoryHandle(0) -> KSharedMemory handle
/// The legacy direct GetSharedMemoryHandle (cmd 1) path is kept for the HLE
/// bridge and older tests.
class HidService final : public IIpcService {
public:
    HidService();

    /// Per-service command ids (subset of the real hid: CMIF).
    enum : u32 {
        Initialize = 0x0,
        CreateAppletResource = 0x1,   // real protocol: first call after Initialize
        GetSharedMemoryHandle = 0x2,  // legacy HLE bridge path
        SetButtonState = 0x20,        ///< HLE bridge: set held-button bitmask
        SetStickState = 0x21,         ///< HLE bridge: set analog stick coords
        UpdateTimestamp = 0x22,       ///< HLE bridge: advance the sampling timestamp
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    /// Guest virtual address of the shared page, or 0 if never initialized.
    [[nodiscard]] vaddr_t GetSharedAddress() const noexcept { return shared_addr_; }

    /// Whether the shared page has been materialised in guest memory.
    [[nodiscard]] bool IsSharedMemoryReady() const noexcept { return shared_addr_ != 0; }

    void SetDebugButtons(u32 buttons) noexcept { debug_buttons_ = buttons; }
    [[nodiscard]] u32 GetDebugButtons() const noexcept { return debug_buttons_; }

    /// Updates the guest shared memory pad state directly from host gamepad polling
    void UpdatePadState(memory::VirtualMemory& mem, u32 buttons, s16 lx, s16 ly, s16 rx, s16 ry);

private:
    u32 HandleCreateAppletResource(const IpcContext& ctx, IpcReplyWriter& reply);
    // Public (below) so the IAppletResource session can delegate; kept near
    // private helpers for clarity.
public:
    u32 HandleGetSharedMemoryHandle(const IpcContext& ctx, IpcReplyWriter& reply);
private:
    u32 HandleSetButtonState(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply);
    u32 HandleSetStickState(const IpcContext& ctx, const IpcRequestReader& request,
                            IpcReplyWriter& reply);
    u32 HandleUpdateTimestamp(IpcReplyWriter& reply);

    void CommitSharedImage(memory::VirtualMemory& mem) const;

    vaddr_t shared_addr_{0};
    bool shared_created_{false};
    u32 debug_buttons_{0};
    s16 debug_lx_{0}, debug_ly_{0};
    s16 debug_rx_{0}, debug_ry_{0};
    u64 sample_counter_{0};
};

} // namespace nemu::core::kernel::ipc