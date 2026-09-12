#pragma once

#include "ipc_service.hpp"
#include "hid_shared.hpp"
#include "k_shared_memory.hpp"

namespace nemu::core::memory {
class VirtualMemory;
} // namespace nemu::core::memory

namespace nemu::core::kernel::ipc {

/// hid: service. HLE for the Switch HID system service. It maps a shared-memory
/// page into the guest and exposes module-scoped HLE update commands so the
/// emulator frontend (or tests) can inject button / stick state that guest
/// input code reads from the shared pad entries.
class HidService final : public IIpcService {
public:
    HidService();

    /// Per-service command ids (subset of the real hid: CMIF).
    enum : u32 {
        Initialize = 0x0,
        GetSharedMemoryHandle = 0x1,
        SetButtonState = 0x20,   ///< HLE bridge: set held-button bitmask
        SetStickState = 0x21,    ///< HLE bridge: set analog stick coords
        UpdateTimestamp = 0x22,  ///< HLE bridge: advance the sampling timestamp
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
    u32 HandleGetSharedMemoryHandle(const IpcContext& ctx, IpcReplyWriter& reply);
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