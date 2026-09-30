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
    u32 hashed_size{0};
};

class Pfs0Archive {
public:
    static constexpr u32 PFS0_MAGIC = 0x30534650; // 'PFS0'
    static constexpr u32 HFS0_MAGIC = 0x30534648; // 'HFS0'

    /// File-entry strides seen in the wild, smallest first:
    ///   0x18 - plain HFS0
    ///   0x38 - HFS1/PFS0 variant carrying a 0x20-byte per-file hash
    ///   0x40 - HFS0 with a reserved u64 *and* a per-file hash (retail carts)
    static constexpr u32 kEntrySizeHfs0 = 0x18;
    static constexpr u32 kEntrySizeHfs1 = 0x38;
    static constexpr u32 kEntrySizeHashed = 0x40;

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

    /// Size of the container header (string table end rounded up to 0x200).
    /// All file data starts at this offset relative to the container start.
    [[nodiscard]] u64 HeaderSize() const noexcept { return header_size_; }

    /// Stride of the file entries actually used, 0 if uninitialised.
    [[nodiscard]] u32 EntryStride() const noexcept { return entry_stride_; }

private:
    std::span<const u8> raw_data_;
    std::vector<Pfs0FileEntry> files_;
    u64 data_offset_base_{0};
    u64 header_size_{0};
    u32 entry_stride_{0};
    bool is_hfs0_{false};

    /// Try to interpret the entry table with `stride`; on success fills `out`.
    bool TryParseEntries(std::span<const u8> data, u32 stride,
                         std::vector<Pfs0FileEntry>& out) const;
};

} // namespace nemu::core::loader
