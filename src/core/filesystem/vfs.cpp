#include "vfs.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <algorithm>
#include <system_error>

namespace nemu::core::filesystem {

VirtualFileSystem::VirtualFileSystem() = default;
VirtualFileSystem::~VirtualFileSystem() = default;

std::string VirtualFileSystem::NormalizePrefix(std::string_view prefix) {
    std::string norm(prefix);
    // Convert to lowercase
    std::transform(norm.begin(), norm.end(), norm.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (norm.ends_with(":/")) {
        return norm;
    }
    if (norm.ends_with(':')) {
        norm += '/';
        return norm;
    }
    norm += ":/";
    return norm;
}

std::optional<std::filesystem::path> VirtualFileSystem::SanitizeRelativePath(std::string_view rel_path) {
    std::string sanitized;
    sanitized.reserve(rel_path.size());

    // Replace backslashes with forward slashes
    for (char c : rel_path) {
        if (c == '\\') {
            sanitized.push_back('/');
        } else {
            sanitized.push_back(c);
        }
    }

    // Strip leading slashes
    size_t start = 0;
    while (start < sanitized.size() && sanitized[start] == '/') {
        ++start;
    }

    std::filesystem::path p(sanitized.substr(start));
    std::filesystem::path normalized = p.lexically_normal();

    // Check for traversal that climbs out (starts with "..")
    for (const auto& part : normalized) {
        if (part == "..") {
            return std::nullopt;
        }
    }

    return normalized;
}

bool VirtualFileSystem::Mount(std::string_view mount_prefix, const std::filesystem::path& host_path, bool read_only) {
    std::lock_guard lock(vfs_mutex_);
    const std::string prefix = NormalizePrefix(mount_prefix);

    std::error_code ec;
    std::filesystem::path canonical_root = std::filesystem::weakly_canonical(host_path, ec);
    if (ec) {
        NEMU_LOG_ERROR("VFS", "Failed to resolve host mount path '{}': {}", host_path.string(), ec.message());
        return false;
    }

    // Ensure host directory exists if creating a writable mount
    if (!read_only && !std::filesystem::exists(canonical_root, ec)) {
        std::filesystem::create_directories(canonical_root, ec);
        if (ec) {
            NEMU_LOG_ERROR("VFS", "Failed to create mount directory '{}': {}", canonical_root.string(), ec.message());
            return false;
        }
    }

    mounts_[prefix] = MountPoint{
        .host_root = canonical_root,
        .read_only = read_only
    };

    NEMU_LOG_INFO("VFS", "Mounted '{}' -> '{}' (read_only: {})", prefix, canonical_root.string(), read_only);
    return true;
}

bool VirtualFileSystem::Unmount(std::string_view mount_prefix) {
    std::lock_guard lock(vfs_mutex_);
    const std::string prefix = NormalizePrefix(mount_prefix);
    return mounts_.erase(prefix) > 0;
}

bool VirtualFileSystem::IsMounted(std::string_view mount_prefix) const {
    std::lock_guard lock(vfs_mutex_);
    const std::string prefix = NormalizePrefix(mount_prefix);
    return mounts_.contains(prefix);
}

std::optional<std::filesystem::path> VirtualFileSystem::ResolvePath(std::string_view virtual_path) const {
    std::lock_guard lock(vfs_mutex_);

    // Find mount delimiter ":/"
    const size_t delim_pos = virtual_path.find(":/");
    if (delim_pos == std::string_view::npos) {
        NEMU_LOG_WARN("VFS", "ResolvePath failed: Missing ':/' delimiter in '{}'", virtual_path);
        return std::nullopt;
    }

    const std::string prefix = NormalizePrefix(virtual_path.substr(0, delim_pos + 2));
    auto it = mounts_.find(prefix);
    if (it == mounts_.end()) {
        NEMU_LOG_WARN("VFS", "ResolvePath failed: Unknown mount prefix in '{}'", virtual_path);
        return std::nullopt;
    }

    const std::string_view rel_part = virtual_path.substr(delim_pos + 2);
    auto clean_rel = SanitizeRelativePath(rel_part);
    if (!clean_rel) {
        NEMU_LOG_WARN("VFS", "Security violation: Path traversal detected in '{}'", virtual_path);
        return std::nullopt;
    }

    const auto& root = it->second.host_root;
    std::filesystem::path target = (root / *clean_rel).lexically_normal();

    std::error_code ec;
    std::filesystem::path canonical_target = std::filesystem::weakly_canonical(target, ec);
    if (ec) {
        canonical_target = target;
    }

    // Verify sandbox containment: target must be inside root
    auto root_it = root.begin();
    auto target_it = canonical_target.begin();
    while (root_it != root.end()) {
        if (target_it == canonical_target.end() || *root_it != *target_it) {
            NEMU_LOG_WARN("VFS", "Security violation: Path '{}' escapes sandbox root '{}'",
                canonical_target.string(), root.string());
            return std::nullopt;
        }
        ++root_it;
        ++target_it;
    }

    return canonical_target;
}

bool VirtualFileSystem::FileExists(std::string_view virtual_path) const {
    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return false;

    std::error_code ec;
    return std::filesystem::is_regular_file(*resolved, ec);
}

bool VirtualFileSystem::DirectoryExists(std::string_view virtual_path) const {
    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return false;

    std::error_code ec;
    return std::filesystem::is_directory(*resolved, ec);
}

bool VirtualFileSystem::CreateDirectories(std::string_view virtual_path) {
    if (IsReadOnly(virtual_path)) {
        NEMU_LOG_WARN("VFS", "Cannot create directories on read-only mount: {}", virtual_path);
        return false;
    }

    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return false;

    std::error_code ec;
    return std::filesystem::create_directories(*resolved, ec) || std::filesystem::exists(*resolved, ec);
}

std::optional<std::vector<u8>> VirtualFileSystem::ReadFile(std::string_view virtual_path) const {
    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return std::nullopt;

    std::ifstream stream(*resolved, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) {
        return std::nullopt;
    }

    const auto size = stream.tellg();
    if (size < 0) {
        return std::nullopt;
    }

    std::vector<u8> buffer(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (size > 0 && !stream.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::nullopt;
    }

    return buffer;
}

bool VirtualFileSystem::WriteFile(std::string_view virtual_path, std::span<const u8> data) {
    if (IsReadOnly(virtual_path)) {
        NEMU_LOG_WARN("VFS", "Cannot write to read-only mount: {}", virtual_path);
        return false;
    }

    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return false;

    std::error_code ec;
    if (resolved->has_parent_path()) {
        std::filesystem::create_directories(resolved->parent_path(), ec);
    }

    std::ofstream stream(*resolved, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    if (!data.empty()) {
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    return stream.good();
}

std::optional<size_t> VirtualFileSystem::GetFileSize(std::string_view virtual_path) const {
    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return std::nullopt;

    std::error_code ec;
    const auto sz = std::filesystem::file_size(*resolved, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<size_t>(sz);
}

bool VirtualFileSystem::DeleteFile(std::string_view virtual_path) {
    if (IsReadOnly(virtual_path)) {
        NEMU_LOG_WARN("VFS", "Cannot delete from read-only mount: {}", virtual_path);
        return false;
    }

    auto resolved = ResolvePath(virtual_path);
    if (!resolved) return false;

    std::error_code ec;
    return std::filesystem::remove(*resolved, ec);
}

bool VirtualFileSystem::IsReadOnly(std::string_view virtual_path) const {
    std::lock_guard lock(vfs_mutex_);
    const size_t delim_pos = virtual_path.find(":/");
    if (delim_pos == std::string_view::npos) {
        return true;
    }
    const std::string prefix = NormalizePrefix(virtual_path.substr(0, delim_pos + 2));
    auto it = mounts_.find(prefix);
    if (it == mounts_.end()) {
        return true;
    }
    return it->second.read_only;
}

} // namespace nemu::core::filesystem
