#pragma once

#include "core/types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace nemu::core::loader {

struct Pfs0FileEntry {
    std::string name;
    u64 offset{0};
    u64 size{0};
};

class Pfs0Archive {
public:
    static constexpr u32 PFS0_MAGIC = 0x30534650; // 'PFS0'
    static constexpr u32 HFS0_MAGIC = 0x30534648; // 'HFS0'

    Pfs0Archive() = default;

    /// Parse a PFS0 / HFS0 / .nsp container from memory
    bool Initialize(std::span<const u8> data);

    /// List all files in the container
    [[nodiscard]] const std::vector<Pfs0FileEntry>& GetFiles() const noexcept { return files_; }

    /// Check if a file exists by name
    [[nodiscard]] bool HasFile(std::string_view name) const;

    /// Read raw file contents by name
    [[nodiscard]] std::optional<std::span<const u8>> OpenFile(std::string_view name) const;

    /// Read raw file contents by index
    [[nodiscard]] std::optional<std::span<const u8>> OpenFile(size_t index) const;

    /// Check if container was HFS0 (e.g. partition within .xci)
    [[nodiscard]] bool IsHfs0() const noexcept { return is_hfs0_; }

    /// Total number of files
    [[nodiscard]] size_t FileCount() const noexcept { return files_.size(); }

private:
    std::span<const u8> raw_data_;
    std::vector<Pfs0FileEntry> files_;
    u64 data_offset_base_{0};
    bool is_hfs0_{false};
};

} // namespace nemu::core::loader
