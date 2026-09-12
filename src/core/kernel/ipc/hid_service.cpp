#include "hid_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

namespace {
    constexpr vaddr_t kSharedBase = 0x00F0000000ULL;
    constexpr size_t kSharedSize = memory::VirtualMemory::PAGE_SIZE;
}

HidService::HidService()
    : IIpcService("hid") {}

u32 HidService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 x_id) {
    (void)request;
    switch (x_id) {
        case Initialize: {
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
            return static_cast<u32>(IpcResult::Success);
        }
        case GetSharedMemoryHandle:
            return HandleGetSharedMemoryHandle(ctx, reply);
        case SetButtonState:
            return HandleSetButtonState(ctx, request, reply);
        case SetStickState:
            return HandleSetStickState(ctx, request, reply);
        case UpdateTimestamp:
            return HandleUpdateTimestamp(reply);
        default:
            NEMU_LOG_WARN("hid", "Unhandled hid command id 0x{:X}", x_id);
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

u32 HidService::HandleGetSharedMemoryHandle(const IpcContext& ctx, IpcReplyWriter& reply) {
    if (!ctx.handle_table || !ctx.memory) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    if (!shared_created_) {
        if (!ctx.memory->Map(kSharedBase, kSharedSize, memory::MemoryPermission::ReadWrite)) {
            NEMU_LOG_ERROR("hid", "Failed to map shared memory at 0x{:016X}", kSharedBase);
            return static_cast<u32>(IpcResult::InvalidBuffer);
        }
        shared_addr_ = kSharedBase;
        shared_created_ = true;
        CommitSharedImage(*ctx.memory);
        NEMU_LOG_DEBUG("hid", "Shared-memory page materialised at 0x{:016X}", shared_addr_);
    }

    auto shmem = std::make_shared<KSharedMemory>();
    shmem->SetAddress(shared_addr_);

    const kernel::Handle handle = ctx.handle_table->CreateHandle(shmem);
    if (handle == kernel::InvalidHandle) {
        return static_cast<u32>(IpcResult::OutOfMemory);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), sizeof(u32));
    reply.Payload<u32>(0, handle);
    return static_cast<u32>(IpcResult::Success);
}

u32 HidService::HandleSetButtonState(const IpcContext& ctx, const IpcRequestReader& request,
                                     IpcReplyWriter& reply) {
    if (!ctx.memory || !shared_created_) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }
    debug_buttons_ = request.Payload<u32>(0);
    ++sample_counter_;
    CommitSharedImage(*ctx.memory);
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 HidService::HandleSetStickState(const IpcContext& ctx, const IpcRequestReader& request,
                                    IpcReplyWriter& reply) {
    if (!ctx.memory || !shared_created_) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }
    // Layout: lx, ly, rx, ry as s16 each.
    debug_lx_ = static_cast<s16>(request.Payload<u16>(0));
    debug_ly_ = static_cast<s16>(request.Payload<u16>(2));
    debug_rx_ = static_cast<s16>(request.Payload<u16>(4));
    debug_ry_ = static_cast<s16>(request.Payload<u16>(6));
    ++sample_counter_;
    CommitSharedImage(*ctx.memory);
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
    return static_cast<u32>(IpcResult::Success);
}

u32 HidService::HandleUpdateTimestamp(IpcReplyWriter& reply) {
    ++sample_counter_;
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 0);
    return static_cast<u32>(IpcResult::Success);
}

void HidService::CommitSharedImage(memory::VirtualMemory& mem) const {
    hid::SharedPadHeader header{};
    header.entry_count = hid::SharedPadHeader::kEntryCount;
    header.entry_size = sizeof(hid::SharedPadEntry);

    header.local.buttons = debug_buttons_;
    header.local.stick_l_x = debug_lx_;
    header.local.stick_l_y = debug_ly_;
    header.local.stick_r_x = debug_rx_;
    header.local.stick_r_y = debug_ry_;
    header.local.timestamp = sample_counter_;

    header.global.buttons = debug_buttons_;
    header.global.timestamp = sample_counter_;

    mem.WriteBlock(shared_addr_, &header, sizeof(header));
}

void HidService::UpdatePadState(memory::VirtualMemory& mem, u32 buttons, s16 lx, s16 ly, s16 rx, s16 ry) {
    debug_buttons_ = buttons;
    debug_lx_ = lx;
    debug_ly_ = ly;
    debug_rx_ = rx;
    debug_ry_ = ry;
    ++sample_counter_;
    if (shared_created_ && shared_addr_ != 0) {
        CommitSharedImage(mem);
    }
}

} // namespace nemu::core::kernel::ipc