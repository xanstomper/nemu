#pragma once

#include "core/types.hpp"
#include "core/filesystem/vfs.hpp"
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace nemu::core::save {

class SaveManager {
public:
    explicit SaveManager(filesystem::VirtualFileSystem& vfs);
    ~SaveManager() = default;

    /// Calculate FNV-1a 64-bit checksum for integrity verification
    [[nodiscard]] static u64 CalculateChecksum(std::span<const u8> data) noexcept;

    /// Atomically write save data with checksum footer and backup rotation
    bool WriteSaveData(u64 title_id, std::string_view filename, std::span<const u8> data);

    /// Read save data with checksum verification; falls back to .bak on corruption
    std::optional<std::vector<u8>> ReadSaveData(u64 title_id, std::string_view filename);

    /// Delete save data and associated backup
    bool DeleteSaveData(u64 title_id, std::string_view filename);

private:
    std::string GetTitleSaveDir(u64 title_id) const;

    filesystem::VirtualFileSystem& vfs_;
};

} // namespace nemu::core::save
