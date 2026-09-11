#pragma once

#include "core/types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <mutex>

namespace nemu::core::filesystem {

class VirtualFileSystem {
public:
    VirtualFileSystem();
    ~VirtualFileSystem();

    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    /// Mount a host filesystem path to a virtual prefix (e.g. "sdmc:/", "romfs:/", "save:/")
    bool Mount(std::string_view mount_prefix, const std::filesystem::path& host_path, bool read_only = false);

    /// Unmount a prefix
    bool Unmount(std::string_view mount_prefix);

    /// Check if a virtual prefix is currently mounted
    bool IsMounted(std::string_view mount_prefix) const;

    /// Resolve a virtual path into a canonical, sandboxed host path.
    /// Returns std::nullopt if the mount prefix is unknown or if path traversal is detected.
    std::optional<std::filesystem::path> ResolvePath(std::string_view virtual_path) const;

    /// Check if a file exists at the given virtual path
    bool FileExists(std::string_view virtual_path) const;

    /// Check if a directory exists at the given virtual path
    bool DirectoryExists(std::string_view virtual_path) const;

    /// Create directories recursively for the given virtual path
    bool CreateDirectories(std::string_view virtual_path);

    /// Read an entire file into memory from the virtual path
    std::optional<std::vector<u8>> ReadFile(std::string_view virtual_path) const;

    /// Write binary data to a file at the virtual path
    bool WriteFile(std::string_view virtual_path, std::span<const u8> data);

    /// Get size of a file at the virtual path in bytes
    std::optional<size_t> GetFileSize(std::string_view virtual_path) const;

    /// Delete a file at the virtual path
    bool DeleteFile(std::string_view virtual_path);

    /// Check if a mount point is read-only
    bool IsReadOnly(std::string_view virtual_path) const;

private:
    struct MountPoint {
        std::filesystem::path host_root;
        bool read_only{false};
    };

    // Normalize mount prefix (ensure trailing ":/")
    static std::string NormalizePrefix(std::string_view prefix);

    // Sanitize and normalize relative path to prevent directory traversal
    static std::optional<std::filesystem::path> SanitizeRelativePath(std::string_view rel_path);

    mutable std::mutex vfs_mutex_;
    std::unordered_map<std::string, MountPoint> mounts_;
};

} // namespace nemu::core::filesystem
