#include "patch_manager.hpp"
#include "platform/logger.hpp"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cctype>

namespace nemu::core::loader {

PatchManager::PatchManager(filesystem::VirtualFileSystem& vfs)
    : vfs_(vfs) {}

std::string PatchManager::FormatBuildId(std::span<const u8> build_id) {
    std::ostringstream oss;
    for (u8 b : build_id) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

std::vector<IpsPatch> PatchManager::ParseIps(std::span<const u8> ips_data) {
    std::vector<IpsPatch> patches;
    if (ips_data.size() < 8) {
        return patches;
    }

    const u8* data = ips_data.data();
    const size_t total_size = ips_data.size();

    // Check for standard 24-bit IPS header: "PATCH"
    if (total_size >= 5 && std::memcmp(data, "PATCH", 5) == 0) {
        size_t offset = 5;
        while (offset + 3 <= total_size) {
            // Check for "EOF" marker
            if (offset + 3 <= total_size && std::memcmp(data + offset, "EOF", 3) == 0) {
                break;
            }

            u32 patch_offset = (static_cast<u32>(data[offset]) << 16) |
                               (static_cast<u32>(data[offset + 1]) << 8) |
                               static_cast<u32>(data[offset + 2]);
            offset += 3;

            if (offset + 2 > total_size) break;
            u16 patch_size = (static_cast<u16>(data[offset]) << 8) |
                              static_cast<u16>(data[offset + 1]);
            offset += 2;

            if (patch_size == 0) {
                // RLE record
                if (offset + 3 > total_size) break;
                u16 rle_size = (static_cast<u16>(data[offset]) << 8) |
                                static_cast<u16>(data[offset + 1]);
                offset += 2;
                u8 rle_byte = data[offset++];

                IpsPatch p;
                p.offset = patch_offset;
                p.data.assign(rle_size, rle_byte);
                patches.push_back(std::move(p));
            } else {
                // Direct record
                if (offset + patch_size > total_size) break;
                IpsPatch p;
                p.offset = patch_offset;
                p.data.assign(data + offset, data + offset + patch_size);
                patches.push_back(std::move(p));
                offset += patch_size;
            }
        }
        return patches;
    }

    // Check for 32-bit IPS32 header: "IPS32"
    if (total_size >= 5 && std::memcmp(data, "IPS32", 5) == 0) {
        size_t offset = 5;
        while (offset + 4 <= total_size) {
            // Check for "EEOF" marker
            if (offset + 4 <= total_size && std::memcmp(data + offset, "EEOF", 4) == 0) {
                break;
            }

            u32 patch_offset = (static_cast<u32>(data[offset]) << 24) |
                               (static_cast<u32>(data[offset + 1]) << 16) |
                               (static_cast<u32>(data[offset + 2]) << 8) |
                               static_cast<u32>(data[offset + 3]);
            offset += 4;

            if (offset + 2 > total_size) break;
            u16 patch_size = (static_cast<u16>(data[offset]) << 8) |
                              static_cast<u16>(data[offset + 1]);
            offset += 2;

            if (patch_size == 0) {
                // RLE record
                if (offset + 3 > total_size) break;
                u16 rle_size = (static_cast<u16>(data[offset]) << 8) |
                                static_cast<u16>(data[offset + 1]);
                offset += 2;
                u8 rle_byte = data[offset++];

                IpsPatch p;
                p.offset = patch_offset;
                p.data.assign(rle_size, rle_byte);
                patches.push_back(std::move(p));
            } else {
                // Direct record
                if (offset + patch_size > total_size) break;
                IpsPatch p;
                p.offset = patch_offset;
                p.data.assign(data + offset, data + offset + patch_size);
                patches.push_back(std::move(p));
                offset += patch_size;
            }
        }
        return patches;
    }

    return patches;
}

bool PatchManager::ApplyPatches(std::span<u8> target_buffer, const std::vector<IpsPatch>& patches) {
    if (patches.empty()) return true;

    size_t applied_count = 0;
    for (const auto& patch : patches) {
        if (patch.offset + patch.data.size() <= target_buffer.size()) {
            std::memcpy(target_buffer.data() + patch.offset, patch.data.data(), patch.data.size());
            ++applied_count;
        } else {
            NEMU_LOG_WARN("PatchManager", "Patch at offset 0x{:X} size 0x{:X} exceeds buffer size 0x{:X}",
                          patch.offset, patch.data.size(), target_buffer.size());
        }
    }

    NEMU_LOG_INFO("PatchManager", "Applied {}/{} patch records cleanly", applied_count, patches.size());
    return applied_count > 0;
}

bool PatchManager::ApplyExeFsPatches(u64 title_id, std::span<const u8> build_id, std::span<u8> nso_image) {
    if (build_id.empty() || nso_image.empty()) return false;

    std::string build_id_hex = FormatBuildId(build_id);
    std::string build_id_upper = build_id_hex;
    for (char& c : build_id_upper) c = static_cast<char>(std::toupper(c));

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << title_id;
    std::string title_id_hex = oss.str();
    std::string title_id_upper = title_id_hex;
    for (char& c : title_id_upper) c = static_cast<char>(std::toupper(c));

    // Candidate search paths
    std::vector<std::string> candidate_paths = {
        "sdmc:/atmosphere/contents/" + title_id_hex + "/exefs/" + build_id_hex + ".ips",
        "sdmc:/atmosphere/contents/" + title_id_upper + "/exefs/" + build_id_upper + ".ips",
        "sdmc:/atmosphere/contents/" + title_id_hex + "/exefs/" + build_id_hex.substr(0, 32) + ".ips",
        "sdmc:/atmosphere/contents/" + title_id_upper + "/exefs/" + build_id_upper.substr(0, 32) + ".ips",
        "sdmc:/atmosphere/exefs_patches/" + build_id_hex + ".ips",
        "sdmc:/atmosphere/exefs_patches/" + build_id_upper + ".ips",
        "sdmc:/atmosphere/exefs_patches/" + build_id_hex.substr(0, 32) + ".ips",
    };

    for (const auto& path : candidate_paths) {
        if (vfs_.FileExists(path)) {
            auto file_data = vfs_.ReadFile(path);
            if (file_data && !file_data->empty()) {
                NEMU_LOG_INFO("PatchManager", "Found IPS patch: {}", path);
                auto patches = ParseIps(*file_data);
                if (!patches.empty()) {
                    return ApplyPatches(nso_image, patches);
                }
            }
        }
    }

    return false;
}

} // namespace nemu::core::loader
