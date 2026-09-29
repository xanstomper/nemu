#pragma once

#include "core/types.hpp"
#include <span>
#include <vector>
#include <optional>
#include <string>

namespace nemu::core::loader {

class NczDecompressor {
public:
    static constexpr u64 NCZSECTN_MAGIC = 0x4E544345535A434EULL; // "NCZSECTN" little-endian
    static constexpr u64 NCZBLOCK_MAGIC = 0x4B434F4C425A434EULL; // "NCZBLOCK" little-endian
    static constexpr u32 ZSTD_MAGIC     = 0xFD2FB528;           // Standard Zstandard frame magic
    static constexpr size_t HEADER_SIZE = 0x4000;               // 16 KiB NCA header in NCZ

    /// Check if buffer represents an NCZ-compressed container
    [[nodiscard]] static bool IsNcz(std::span<const u8> data);

    /// Decompress an NCZ buffer into a standard uncompressed NCA container
    [[nodiscard]] static std::optional<std::vector<u8>> Decompress(std::span<const u8> data);

    /// Check if multi-part split volume (e.g. .xc0, .ns0, .00)
    [[nodiscard]] static bool IsSplitVolume(const std::string& path);

    /// Get canonical base extension for a split volume (e.g. ".xc0" -> ".xci", ".ns0" -> ".nsp")
    [[nodiscard]] static std::string GetCanonicalExtension(const std::string& path);

    /// Collect all contiguous split volume files belonging to a sequence
    [[nodiscard]] static std::vector<std::string> GetSplitParts(const std::string& first_part_path);
};

} // namespace nemu::core::loader
