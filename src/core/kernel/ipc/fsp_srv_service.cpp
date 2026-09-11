#include "fsp_srv_service.hpp"
#include "core/kernel/k_handle_table.hpp"
#include "core/kernel/ipc/ipc_service.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// FileSystemFileService
// ---------------------------------------------------------------------------

FileSystemFileService::FileSystemFileService(
    std::shared_ptr<filesystem::VirtualFileSystem> vfs,
    std::string path,
    u32 mode
) : IIpcService("fsp-srv:IFile"), vfs_(std::move(vfs)), path_(std::move(path)), mode_(mode) {}

u32 FileSystemFileService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case Read: {
            const u64 offset = request.Payload<u64>(0);
            const u64 size = request.Payload<u64>(8);

            auto file_bytes = vfs_->ReadFile(path_);
            u64 bytes_read = 0;
            if (file_bytes.has_value() && offset < file_bytes->size()) {
                const u64 avail = file_bytes->size() - offset;
                bytes_read = std::min(size, avail);
                // If destination pointer is provided in payload or caller wants inline
                // Write inline into reply payload at offset 8 if small enough
                if (bytes_read <= 128) {
                    for (u64 i = 0; i < bytes_read; ++i) {
                        reply.Write<u8>(static_cast<size_t>(IpcField::Payload) + 8 + i, (*file_bytes)[offset + i]);
                    }
                }
            }

            reply.Begin(0, 16);
            reply.Payload<u32>(0, 0); // Result = Success
            reply.Payload<u64>(4, bytes_read);
            return static_cast<u32>(IpcResult::Success);
        }

        case Write: {
            const u64 offset = request.Payload<u64>(0);
            const u64 size = request.Payload<u64>(8);
            (void)offset;

            // Simple write implementation
            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, static_cast<u32>(size));
            return static_cast<u32>(IpcResult::Success);
        }

        case Flush: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case SetSize: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSize: {
            auto sz = vfs_->GetFileSize(path_);
            u64 file_sz = sz.value_or(0);

            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, file_sz);
            return static_cast<u32>(IpcResult::Success);
        }

        case OperateRange: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            NEMU_LOG_WARN("fsp-srv", "IFile: Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

// ---------------------------------------------------------------------------
// FileSystemStorageService
// ---------------------------------------------------------------------------

FileSystemStorageService::FileSystemStorageService(
    std::shared_ptr<filesystem::VirtualFileSystem> vfs,
    std::string path
) : IIpcService("fsp-srv:IStorage"), vfs_(std::move(vfs)), path_(std::move(path)) {}

u32 FileSystemStorageService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)ctx;
    (void)request;

    switch (x_id) {
        case Read: {
            const u64 offset = request.Payload<u64>(0);
            const u64 size = request.Payload<u64>(8);

            auto file_bytes = vfs_->ReadFile(path_);
            u64 bytes_read = 0;
            if (file_bytes.has_value() && offset < file_bytes->size()) {
                const u64 avail = file_bytes->size() - offset;
                bytes_read = std::min(size, avail);
            }

            reply.Begin(0, 16);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, bytes_read);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetSize: {
            auto sz = vfs_->GetFileSize(path_);
            u64 file_sz = sz.value_or(0);

            reply.Begin(0, 12);
            reply.Payload<u32>(0, 0);
            reply.Payload<u64>(4, file_sz);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// FileSystemSubService
// ---------------------------------------------------------------------------

FileSystemSubService::FileSystemSubService(
    std::shared_ptr<filesystem::VirtualFileSystem> vfs,
    std::string root_prefix
) : IIpcService("fsp-srv:IFileSystem"), vfs_(std::move(vfs)), root_prefix_(std::move(root_prefix)) {}

u32 FileSystemSubService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    switch (x_id) {
        case CreateFile: {
            auto name = request.ReadString(64, 8);
            std::string full_path = root_prefix_ + std::string(name);
            vfs_->WriteFile(full_path, std::span<const u8>());

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case DeleteFile: {
            auto name = request.ReadString(64, 0);
            std::string full_path = root_prefix_ + std::string(name);
            vfs_->DeleteFile(full_path);

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case CreateDirectory: {
            auto name = request.ReadString(64, 0);
            std::string full_path = root_prefix_ + std::string(name);
            vfs_->CreateDirectories(full_path);

            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case DeleteDirectory:
        case DeleteDirectoryRecursively:
        case CleanDirectoryRecursively: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenFile: {
            u32 mode = request.Payload<u32>(0);
            auto name = request.ReadString(64, 4);
            std::string full_path = root_prefix_ + std::string(name);

            auto file_svc = std::make_shared<FileSystemFileService>(vfs_, full_path, mode);
            auto session = std::make_shared<KClientSession>();
            session->SetService(file_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0); // Result Success
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case Commit: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case GetEntryType: {
            auto name = request.ReadString(64, 0);
            std::string full_path = root_prefix_ + std::string(name);

            u32 entry_type = 0; // 0 = unknown, 1 = file, 2 = directory
            if (vfs_->FileExists(full_path)) {
                entry_type = 1;
            } else if (vfs_->DirectoryExists(full_path)) {
                entry_type = 2;
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, entry_type);
            return static_cast<u32>(IpcResult::Success);
        }

        default:
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

// ---------------------------------------------------------------------------
// FspSrvService
// ---------------------------------------------------------------------------

FspSrvService::FspSrvService(std::shared_ptr<filesystem::VirtualFileSystem> vfs)
    : IIpcService("fsp-srv"), vfs_(std::move(vfs)) {}

u32 FspSrvService::HandleRequest(
    const IpcContext& ctx,
    const IpcRequestReader& request,
    IpcReplyWriter& reply,
    u32 x_id
) {
    (void)request;

    switch (x_id) {
        case SetCurrentProcess: {
            reply.Begin(0, 4);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenFileSystem: {
            auto sub_svc = std::make_shared<FileSystemSubService>(vfs_, "sdmc:/");
            auto session = std::make_shared<KClientSession>();
            session->SetService(sub_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenDataStorageByCurrentProcess:
        case OpenDataStorageByProgramId: {
            auto storage_svc = std::make_shared<FileSystemStorageService>(vfs_, "romfs:/data.bin");
            auto session = std::make_shared<KClientSession>();
            session->SetService(storage_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenSaveDataFileSystem:
        case OpenDirectorySaveDataFileSystem: {
            auto save_svc = std::make_shared<FileSystemSubService>(vfs_, "save:/");
            auto session = std::make_shared<KClientSession>();
            session->SetService(save_svc);

            Handle session_handle = 0;
            if (ctx.handle_table) {
                session_handle = ctx.handle_table->CreateHandle(session);
            }

            reply.Begin(0, 8);
            reply.Payload<u32>(0, 0);
            reply.Payload<u32>(4, session_handle);
            return static_cast<u32>(IpcResult::Success);
        }

        case OpenSdCardFileSystem: {
            auto sdmc_svc = std::make_shared<FileSystemSubService>(vfs_, "sdmc:/");
            auto session = std::make_shared<KClientSession>();
            session->SetService(sdmc_svc);

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
            NEMU_LOG_WARN("fsp-srv", "Unhandled command 0x{:X}", x_id);
            reply.Begin(0, 4);
            reply.Payload<u32>(0, static_cast<u32>(IpcResult::Unimplemented));
            return static_cast<u32>(IpcResult::Unimplemented);
    }
}

} // namespace nemu::core::kernel::ipc
