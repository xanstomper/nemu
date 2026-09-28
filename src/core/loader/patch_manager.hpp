#pragma once

#include "core/types.hpp"
#include "core/filesystem/vfs.hpp"
#include <span>
#include <string>
#include <vector>
#include <array>
#include <optional>

namespace nemu::core::loader {

struct IpsPatch {
    u64 offset{0};
    std::vector<u8> data;
};

class PatchManager {
public:
    explicit PatchManager(filesystem::VirtualFileSystem& vfs);

    /// Formats a 32-byte module ID / build ID into a hex string
    static std::string FormatBuildId(std::span<const u8> build_id);

    /// Parses an IPS or IPS32 binary stream into a list of offset/data patches
    static std::vector<IpsPatch> ParseIps(std::span<const u8> ips_data);

    /// Applies patches directly to an in-memory buffer (e.g. decompressed NSO segment or image)
    static bool ApplyPatches(std::span<u8> target_buffer, const std::vector<IpsPatch>& patches);

    /// Discovers and applies IPS patches for a given title_id and build_id
    /// Searches Atmosphere standard paths:
    /// - sdmc:/atmosphere/contents/<title_id_hex>/exefs/<build_id>.ips
    /// - sdmc:/atmosphere/exefs_patches/<build_id>.ips
    bool ApplyExeFsPatches(u64 title_id, std::span<const u8> build_id, std::span<u8> nso_image);

private:
    filesystem::VirtualFileSystem& vfs_;
};

} // namespace nemu::core::loader
