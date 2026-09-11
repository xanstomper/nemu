#include "save_manager.hpp"
#include "platform/logger.hpp"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace nemu::core::save {

namespace {
    constexpr u32 SAVE_MAGIC = 0x534D454E; // 'NEMS'
}

SaveManager::SaveManager(filesystem::VirtualFileSystem& vfs)
    : vfs_(vfs) {
}

u64 SaveManager::CalculateChecksum(std::span<const u8> data) noexcept {
    u64 hash = 0xcbf29ce484222325ULL;
    for (u8 b : data) {
        hash ^= b;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string SaveManager::GetTitleSaveDir(u64 title_id) const {
    std::ostringstream ss;
    ss << "save:/" << std::hex << std::setw(16) << std::setfill('0') << title_id;
    return ss.str();
}

bool SaveManager::WriteSaveData(u64 title_id, std::string_view filename, std::span<const u8> data) {
    const std::string dir = GetTitleSaveDir(title_id);
    vfs_.CreateDirectories(dir);

    const std::string target_path = dir + "/" + std::string(filename);
    const std::string backup_path = target_path + ".bak";
    const std::string temp_path   = target_path + ".tmp";

    // 1. If existing file exists and is valid, preserve as backup
    if (vfs_.FileExists(target_path)) {
        auto existing = vfs_.ReadFile(target_path);
        if (existing) {
            vfs_.WriteFile(backup_path, *existing);
        }
    }

    // 2. Prepare container with magic header and checksum footer
    const u64 checksum = CalculateChecksum(data);
    const size_t total_size = sizeof(SAVE_MAGIC) + data.size() + sizeof(checksum);
    std::vector<u8> container(total_size);
    if (container.empty() || container.data() == nullptr) {
        return false;
    }

    std::memcpy(container.data(), &SAVE_MAGIC, sizeof(SAVE_MAGIC));
    if (!data.empty()) {
        std::memcpy(container.data() + sizeof(SAVE_MAGIC), data.data(), data.size());
    }
    std::memcpy(container.data() + sizeof(SAVE_MAGIC) + data.size(), &checksum, sizeof(checksum));

    // 3. Write to temporary file first (atomic staging)
    if (!vfs_.WriteFile(temp_path, container)) {
        NEMU_LOG_ERROR("Save", "Failed to stage atomic save to '{}'", temp_path);
        return false;
    }

    // 4. Commit to target file
    if (!vfs_.WriteFile(target_path, container)) {
        NEMU_LOG_ERROR("Save", "Failed to commit save to '{}'", target_path);
        return false;
    }

    // 5. Clean up temporary file
    vfs_.DeleteFile(temp_path);

    NEMU_LOG_INFO("Save", "Saved {} bytes for title 0x{:016X} to '{}' (checksum: 0x{:016X})",
        data.size(), title_id, target_path, checksum);

    return true;
}

std::optional<std::vector<u8>> SaveManager::ReadSaveData(u64 title_id, std::string_view filename) {
    const std::string dir = GetTitleSaveDir(title_id);
    const std::string target_path = dir + "/" + std::string(filename);
    const std::string backup_path = target_path + ".bak";

    auto try_read = [&](const std::string& path) -> std::optional<std::vector<u8>> {
        auto container = vfs_.ReadFile(path);
        if (!container || container->size() < sizeof(SAVE_MAGIC) + sizeof(u64)) {
            return std::nullopt;
        }

        u32 magic = 0;
        std::memcpy(&magic, container->data(), sizeof(magic));
        if (magic != SAVE_MAGIC) {
            NEMU_LOG_WARN("Save", "Magic mismatch in '{}'", path);
            return std::nullopt;
        }

        const size_t data_size = container->size() - sizeof(SAVE_MAGIC) - sizeof(u64);
        u64 stored_checksum = 0;
        std::memcpy(&stored_checksum, container->data() + sizeof(SAVE_MAGIC) + data_size, sizeof(stored_checksum));

        std::span<const u8> data_span(container->data() + sizeof(SAVE_MAGIC), data_size);
        const u64 computed_checksum = CalculateChecksum(data_span);

        if (stored_checksum != computed_checksum) {
            NEMU_LOG_WARN("Save", "Checksum mismatch in '{}' (stored: 0x{:X}, computed: 0x{:X})",
                path, stored_checksum, computed_checksum);
            return std::nullopt;
        }

        std::vector<u8> result(data_size);
        if (data_size > 0) {
            std::memcpy(result.data(), data_span.data(), data_size);
        }
        return result;
    };

    // Try primary save file
    auto primary = try_read(target_path);
    if (primary) {
        return primary;
    }

    // Try backup if primary failed or corrupted
    NEMU_LOG_WARN("Save", "Primary save '{}' failed verification; attempting backup recovery...", target_path);
    auto backup = try_read(backup_path);
    if (backup) {
        NEMU_LOG_INFO("Save", "Successfully recovered save from backup '{}'", backup_path);
        // Restore backup as primary
        WriteSaveData(title_id, filename, *backup);
        return backup;
    }

    NEMU_LOG_ERROR("Save", "Both primary and backup save files corrupted or missing for title 0x{:016X}", title_id);
    return std::nullopt;
}

bool SaveManager::DeleteSaveData(u64 title_id, std::string_view filename) {
    const std::string dir = GetTitleSaveDir(title_id);
    const std::string target_path = dir + "/" + std::string(filename);
    const std::string backup_path = target_path + ".bak";

    bool deleted = vfs_.DeleteFile(target_path);
    vfs_.DeleteFile(backup_path);
    return deleted;
}

} // namespace nemu::core::save
