#pragma once

#include "core/types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <unordered_map>
#include <filesystem>

namespace nemu::core::filesystem {
class VirtualFileSystem;
}

namespace nemu::core::loader {

#pragma pack(push, 1)
struct RomfsHeader {
    u64 header_size{0x50};
    u64 dir_hash_table_offset{0};
    u64 dir_hash_table_size{0};
    u64 dir_meta_table_offset{0};
    u64 dir_meta_table_size{0};
    u64 file_hash_table_offset{0};
    u64 file_hash_table_size{0};
    u64 file_meta_table_offset{0};
    u64 file_meta_table_size{0};
    u64 data_offset{0};
};

struct RomfsIvfcHeader {
    u32 magic{0x43465649}; // 'IVFC'
    u32 id{0};
    u32 master_hash_size{0};
    u32 number_of_levels{6};
};

struct RomfsIvfcLevel {
    u64 logical_offset{0};
    u64 hash_data_size{0};
    u32 block_size_log2{0};
    u32 reserved{0};
};

struct RomfsDirEntryHeader {
    u32 parent_offset{0};
    u32 sibling_offset{0xFFFFFFFF};
    u32 child_dir_offset{0xFFFFFFFF};
    u32 child_file_offset{0xFFFFFFFF};
    u32 next_hash{0xFFFFFFFF};
    u32 name_length{0};
};

struct RomfsFileEntryHeader {
    u32 parent_dir_offset{0};
    u32 sibling_offset{0xFFFFFFFF};
    u64 data_offset{0};
    u64 data_size{0};
    u32 next_hash{0xFFFFFFFF};
    u32 name_length{0};
};
#pragma pack(pop)

struct RomfsFile {
    std::string path;
    u64 data_offset{0};
    u64 data_size{0};
};

class RomfsReader {
public:
    static constexpr u32 IVFC_MAGIC = 0x43465649; // 'IVFC'

    RomfsReader() = default;
    ~RomfsReader() = default;

    /// Parse a RomFS binary image (with optional IVFC header)
    bool Initialize(std::span<const u8> data);

    [[nodiscard]] bool HasFile(std::string_view path) const;
    [[nodiscard]] std::optional<std::span<const u8>> OpenFile(std::string_view path) const;
    [[nodiscard]] std::optional<size_t> GetFileSize(std::string_view path) const;
    [[nodiscard]] const std::unordered_map<std::string, RomfsFile>& GetFiles() const noexcept { return files_; }
    [[nodiscard]] std::vector<std::string> GetFileList() const;

    /// Extract all files to host directory
    bool DumpToDirectory(const std::filesystem::path& dest_dir) const;

    /// Mount files into VFS under mount prefix (e.g. "romfs:/") via host directory staging
    bool MountToVfs(filesystem::VirtualFileSystem& vfs, const std::filesystem::path& staging_dir, std::string_view mount_prefix = "romfs:/") const;

    /// Builder helper to create a valid RomFS image from virtual files in memory
    static std::vector<u8> BuildRomfs(const std::unordered_map<std::string, std::vector<u8>>& input_files);

private:
    void TraverseDirectory(u32 dir_offset, const std::string& current_path);
    static std::string NormalizePath(std::string_view path);

    std::span<const u8> raw_data_;
    u64 romfs_base_offset_{0};
    RomfsHeader header_{};
    std::unordered_map<std::string, RomfsFile> files_;
};

} // namespace nemu::core::loader
