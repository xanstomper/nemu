#include "pl_service.hpp"
#include "k_shared_memory.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/memory/virtual_memory.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::kernel::ipc {

namespace {
    constexpr vaddr_t kFontSharedBase = 0x00E0000000ULL;
    constexpr size_t kFontSharedSize = 0x02000000ULL; // 32 MiB

    constexpr u32 kFontSizes[static_cast<size_t>(SharedFontType::TotalFonts)] = {
        0x01100000, // Standard: ~17 MB
        0x00400000, // ChineseSimplified: 4 MB
        0x00400000, // ExtChineseSimplified: 4 MB
        0x00400000, // ChineseTraditional: 4 MB
        0x00400000, // Korean: 4 MB
        0x00100000  // NintendoExtension: 1 MB
    };

    void PopulateSyntheticFontData(u8* buffer, size_t size) {
        if (!buffer || size < 4096) return;
        std::memset(buffer, 0, size);

        // Minimal OpenType / TrueType font header:
        // sfnt version 0x00010000 (TrueType outlines)
        buffer[0] = 0x00; buffer[1] = 0x01; buffer[2] = 0x00; buffer[3] = 0x00;
        // numTables = 4
        buffer[4] = 0x00; buffer[5] = 0x04;
        // searchRange = (4 largest power of 2 <= 4) * 16 = 64 = 0x0040
        buffer[6] = 0x00; buffer[7] = 0x40;
        // entrySelector = log2(4) = 2
        buffer[8] = 0x00; buffer[9] = 0x02;
        // rangeShift = (4 * 16) - 64 = 0
        buffer[10] = 0x00; buffer[11] = 0x00;

        // Table record 1: 'cmap'
        std::memcpy(buffer + 12, "cmap", 4);
        // Table record 2: 'head'
        std::memcpy(buffer + 28, "head", 4);
        // Table record 3: 'hhea'
        std::memcpy(buffer + 44, "hhea", 4);
        // Table record 4: 'maxp'
        std::memcpy(buffer + 60, "maxp", 4);
    }
}

PlService::PlService(std::string name)
    : IIpcService(std::move(name)) {}

u32 PlService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                             IpcReplyWriter& reply, u32 x_id) {
    switch (x_id) {
        case RequestSharedFont:
            return HandleRequestSharedFont(request, reply);
        case GetSharedFontLoadState:
            return HandleGetSharedFontLoadState(request, reply);
        case GetSharedFontSize:
            return HandleGetSharedFontSize(request, reply);
        case GetSharedFontAddress:
            return HandleGetSharedFontAddress(request, reply);
        case GetSharedFontSharedMemory:
            return HandleGetSharedFontSharedMemory(ctx, reply);
        default:
            NEMU_LOG_WARN("pl", "{}: Unhandled command 0x{:X}", GetName(), x_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

bool PlService::IsFontLoaded(SharedFontType type) const noexcept {
    const auto idx = static_cast<u32>(type);
    return idx < static_cast<u32>(SharedFontType::TotalFonts);
}

u32 PlService::GetFontSize(SharedFontType type) const noexcept {
    const auto idx = static_cast<u32>(type);
    if (idx < static_cast<u32>(SharedFontType::TotalFonts)) {
        return kFontSizes[idx];
    }
    return 0;
}

u32 PlService::HandleRequestSharedFont(const IpcRequestReader& request, IpcReplyWriter& reply) {
    const u32 font_type = request.Payload<u32>(0);
    NEMU_LOG_DEBUG("pl", "RequestSharedFont type={}", font_type);
    reply.Begin(static_cast<u32>(IpcCommandType::Request), 4);
    reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
    return static_cast<u32>(IpcResult::Success);
}

u32 PlService::HandleGetSharedFontLoadState(const IpcRequestReader& request, IpcReplyWriter& reply) {
    const u32 font_type = request.Payload<u32>(0);
    const bool valid = (font_type < static_cast<u32>(SharedFontType::TotalFonts));
    const u32 state = valid ? 1 : 0; // 1 = Loaded

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
    reply.Payload<u32>(4, state);
    return static_cast<u32>(IpcResult::Success);
}

u32 PlService::HandleGetSharedFontSize(const IpcRequestReader& request, IpcReplyWriter& reply) {
    const u32 font_type = request.Payload<u32>(0);
    u32 sz = 0;
    if (font_type < static_cast<u32>(SharedFontType::TotalFonts)) {
        sz = kFontSizes[font_type];
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
    reply.Payload<u32>(4, sz);
    return static_cast<u32>(IpcResult::Success);
}

u32 PlService::HandleGetSharedFontAddress(const IpcRequestReader& request, IpcReplyWriter& reply) {
    const u32 font_type = request.Payload<u32>(0);
    vaddr_t offset = 0;
    for (u32 i = 0; i < font_type && i < static_cast<u32>(SharedFontType::TotalFonts); ++i) {
        offset += kFontSizes[i];
    }
    const vaddr_t font_addr = shared_addr_ + offset;

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 12);
    reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
    reply.Payload<u64>(4, font_addr);
    return static_cast<u32>(IpcResult::Success);
}

bool PlService::EnsureSharedMemoryCreated(const IpcContext& ctx) {
    if (shared_created_) return true;
    if (!ctx.memory) return false;

    if (!ctx.memory->Map(kFontSharedBase, kFontSharedSize, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("pl", "Failed to map shared font memory at 0x{:016X}", kFontSharedBase);
        return false;
    }

    shared_addr_ = kFontSharedBase;
    shared_created_ = true;

    u8* ptr = ctx.memory->GetPointer(shared_addr_);
    if (ptr) {
        PopulateSyntheticFontData(ptr, kFontSharedSize);
    }

    NEMU_LOG_INFO("pl", "Initialized shared font memory at 0x{:016X} ({} MiB)",
                  shared_addr_, kFontSharedSize / (1024 * 1024));
    return true;
}

u32 PlService::HandleGetSharedFontSharedMemory(const IpcContext& ctx, IpcReplyWriter& reply) {
    if (!ctx.handle_table || !ctx.memory) {
        return static_cast<u32>(IpcResult::InvalidBuffer);
    }

    if (!EnsureSharedMemoryCreated(ctx)) {
        return static_cast<u32>(IpcResult::OutOfMemory);
    }

    auto shmem = std::make_shared<KSharedMemory>();
    shmem->SetAddress(shared_addr_);

    const kernel::Handle handle = ctx.handle_table->CreateHandle(shmem);
    if (handle == kernel::InvalidHandle) {
        return static_cast<u32>(IpcResult::OutOfMemory);
    }

    reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
    reply.Payload<u32>(0, static_cast<u32>(IpcResult::Success));
    reply.Payload<u32>(4, handle);
    return static_cast<u32>(IpcResult::Success);
}

} // namespace nemu::core::kernel::ipc
