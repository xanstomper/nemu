#pragma once

#include "ipc_service.hpp"
#include "core/filesystem/vfs.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel::ipc {

class FileSystemFileService final : public IIpcService {
public:
    FileSystemFileService(std::shared_ptr<filesystem::VirtualFileSystem> vfs, std::string path, u32 mode);
    ~FileSystemFileService() override = default;

    enum : u32 {
        Read = 0x0,
        Write = 0x1,
        Flush = 0x2,
        SetSize = 0x3,
        GetSize = 0x4,
        OperateRange = 0x5,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<filesystem::VirtualFileSystem> vfs_;
    std::string path_;
    u32 mode_{0};
};

class FileSystemStorageService final : public IIpcService {
public:
    explicit FileSystemStorageService(std::shared_ptr<filesystem::VirtualFileSystem> vfs, std::string path);
    ~FileSystemStorageService() override = default;

    enum : u32 {
        Read = 0x0,
        Write = 0x1,
        Flush = 0x2,
        SetSize = 0x3,
        GetSize = 0x4,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<filesystem::VirtualFileSystem> vfs_;
    std::string path_;
};

class FileSystemSubService final : public IIpcService {
public:
    explicit FileSystemSubService(std::shared_ptr<filesystem::VirtualFileSystem> vfs, std::string root_prefix);
    ~FileSystemSubService() override = default;

    enum : u32 {
        CreateFile = 0x0,
        DeleteFile = 0x1,
        CreateDirectory = 0x2,
        DeleteDirectory = 0x3,
        DeleteDirectoryRecursively = 0x4,
        CleanDirectoryRecursively = 0x5,
        OpenFile = 0x8,
        OpenDirectory = 0x9,
        Commit = 0xA,
        GetEntryType = 0xB,
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

private:
    std::shared_ptr<filesystem::VirtualFileSystem> vfs_;
    std::string root_prefix_;
};

class FspSrvService final : public IIpcService {
public:
    explicit FspSrvService(std::shared_ptr<filesystem::VirtualFileSystem> vfs);
    ~FspSrvService() override = default;

    enum : u32 {
        OpenFileSystem = 0x0,
        SetCurrentProcess = 0x1,
        OpenDataStorageByCurrentProcess = 0x12,   // 18
        OpenDataStorageByProgramId = 0x14,        // 20
        OpenSaveDataFileSystem = 0x33,            // 51
        OpenDirectorySaveDataFileSystem = 0x34,   // 52
        OpenSdCardFileSystem = 0x65,              // 101
    };

    u32 HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                      IpcReplyWriter& reply, u32 x_id) override;

    [[nodiscard]] std::shared_ptr<filesystem::VirtualFileSystem> GetVfs() const noexcept { return vfs_; }

private:
    std::shared_ptr<filesystem::VirtualFileSystem> vfs_;
};

} // namespace nemu::core::kernel::ipc
