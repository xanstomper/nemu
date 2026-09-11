#include "romfs.hpp"
#include "core/filesystem/vfs.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <fstream>
#include <algorithm>

namespace nemu::core::loader {

std::string RomfsReader::NormalizePath(std::string_view path) {
    std::string s(path);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    for (char& c : s) {
        if (c == '\\') c = '/';
    }

    if (s.starts_with("romfs:/")) {
        s = s.substr(7);
    } else if (s.starts_with("romfs:")) {
        s = s.substr(6);
    }

    while (!s.empty() && s.front() == '/') {
        s.erase(0, 1);
    }
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

bool RomfsReader::Initialize(std::span<const u8> data) {
    files_.clear();
    raw_data_ = data;
    romfs_base_offset_ = 0;

    if (data.size() < sizeof(RomfsHeader)) {
        NEMU_LOG_ERROR("RomFS", "Data size too small for RomFS header: {} bytes", data.size());
        return false;
    }

    // Check for IVFC magic at start
    u32 magic = 0;
    std::memcpy(&magic, data.data(), sizeof(u32));
    if (magic == IVFC_MAGIC) {
        if (data.size() < sizeof(RomfsIvfcHeader) + 6 * sizeof(RomfsIvfcLevel)) {
            NEMU_LOG_ERROR("RomFS", "IVFC header truncated");
            return false;
        }

        const auto* levels = reinterpret_cast<const RomfsIvfcLevel*>(data.data() + 0x20);
        // Level 6 is data level (index 5)
        romfs_base_offset_ = levels[5].logical_offset;

        if (romfs_base_offset_ + sizeof(RomfsHeader) > data.size()) {
            NEMU_LOG_ERROR("RomFS", "IVFC Level 6 offset outside buffer boundaries: 0x{:X}", romfs_base_offset_);
            return false;
        }
    }

    std::memcpy(&header_, data.data() + romfs_base_offset_, sizeof(RomfsHeader));

    if (header_.header_size != 0x50 && header_.header_size != 0x28) {
        NEMU_LOG_WARN("RomFS", "Unexpected RomFS header size: 0x{:X}", header_.header_size);
    }

    const u64 meta_end = romfs_base_offset_ + header_.data_offset;
    if (meta_end > data.size()) {
        NEMU_LOG_ERROR("RomFS", "RomFS metadata tables overflow data buffer (0x{:X} > 0x{:X})", meta_end, data.size());
        return false;
    }

    NEMU_LOG_INFO("RomFS", "Initializing RomFS: Data offset 0x{:X}, total size 0x{:X}", header_.data_offset, data.size());

    // Traverse root directory (offset 0 in directory metadata table)
    TraverseDirectory(0, "");

    NEMU_LOG_INFO("RomFS", "Indexed {} files from RomFS image", files_.size());
    return true;
}

void RomfsReader::TraverseDirectory(u32 dir_offset, const std::string& current_path) {
    if (dir_offset == 0xFFFFFFFF) return;

    const u64 abs_dir_offset = romfs_base_offset_ + header_.dir_meta_table_offset + dir_offset;
    if (abs_dir_offset + sizeof(RomfsDirEntryHeader) > raw_data_.size()) {
        return;
    }

    RomfsDirEntryHeader dir_entry{};
    std::memcpy(&dir_entry, raw_data_.data() + abs_dir_offset, sizeof(RomfsDirEntryHeader));

    std::string dir_name;
    if (dir_entry.name_length > 0) {
        const u64 name_offset = abs_dir_offset + sizeof(RomfsDirEntryHeader);
        if (name_offset + dir_entry.name_length <= raw_data_.size()) {
            dir_name = std::string(reinterpret_cast<const char*>(raw_data_.data() + name_offset), dir_entry.name_length);
        }
    }

    std::string full_dir_path = current_path;
    if (!dir_name.empty()) {
        if (full_dir_path.empty()) {
            full_dir_path = dir_name;
        } else {
            full_dir_path += "/" + dir_name;
        }
    }

    // Traverse files in this directory
    u32 file_offset = dir_entry.child_file_offset;
    size_t file_safety_count = 0;
    while (file_offset != 0xFFFFFFFF && file_safety_count++ < 10000) {
        const u64 abs_file_offset = romfs_base_offset_ + header_.file_meta_table_offset + file_offset;
        if (abs_file_offset + sizeof(RomfsFileEntryHeader) > raw_data_.size()) {
            break;
        }

        RomfsFileEntryHeader file_entry{};
        std::memcpy(&file_entry, raw_data_.data() + abs_file_offset, sizeof(RomfsFileEntryHeader));

        std::string filename;
        if (file_entry.name_length > 0) {
            const u64 fn_offset = abs_file_offset + sizeof(RomfsFileEntryHeader);
            if (fn_offset + file_entry.name_length <= raw_data_.size()) {
                filename = std::string(reinterpret_cast<const char*>(raw_data_.data() + fn_offset), file_entry.name_length);
            }
        }

        if (!filename.empty()) {
            std::string file_path = full_dir_path.empty() ? filename : (full_dir_path + "/" + filename);
            std::string norm = NormalizePath(file_path);
            files_[norm] = RomfsFile{
                .path = file_path,
                .data_offset = file_entry.data_offset,
                .data_size = file_entry.data_size
            };
        }

        file_offset = file_entry.sibling_offset;
    }

    // Traverse child directories recursively
    if (dir_entry.child_dir_offset != 0xFFFFFFFF) {
        TraverseDirectory(dir_entry.child_dir_offset, full_dir_path);
    }

    // Traverse sibling directories recursively
    if (dir_entry.sibling_offset != 0xFFFFFFFF) {
        TraverseDirectory(dir_entry.sibling_offset, current_path);
    }
}

bool RomfsReader::HasFile(std::string_view path) const {
    return files_.contains(NormalizePath(path));
}

std::optional<std::span<const u8>> RomfsReader::OpenFile(std::string_view path) const {
    auto it = files_.find(NormalizePath(path));
    if (it == files_.end()) {
        return std::nullopt;
    }

    const auto& file = it->second;
    const u64 abs_data_offset = romfs_base_offset_ + header_.data_offset + file.data_offset;
    if (abs_data_offset + file.data_size > raw_data_.size()) {
        NEMU_LOG_ERROR("RomFS", "File data out of bounds for '{}': 0x{:X} + 0x{:X} > 0x{:X}",
                       file.path, abs_data_offset, file.data_size, raw_data_.size());
        return std::nullopt;
    }

    return raw_data_.subspan(static_cast<size_t>(abs_data_offset), static_cast<size_t>(file.data_size));
}

std::optional<size_t> RomfsReader::GetFileSize(std::string_view path) const {
    auto it = files_.find(NormalizePath(path));
    if (it == files_.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(it->second.data_size);
}

std::vector<std::string> RomfsReader::GetFileList() const {
    std::vector<std::string> list;
    list.reserve(files_.size());
    for (const auto& [k, v] : files_) {
        list.push_back(v.path);
    }
    std::sort(list.begin(), list.end());
    return list;
}

bool RomfsReader::DumpToDirectory(const std::filesystem::path& dest_dir) const {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) return false;

    for (const auto& [key, file] : files_) {
        auto data_opt = OpenFile(key);
        if (!data_opt) continue;

        std::filesystem::path target = dest_dir / file.path;
        std::filesystem::create_directories(target.parent_path(), ec);

        std::ofstream out(target, std::ios::binary);
        if (out.is_open() && !data_opt->empty()) {
            out.write(reinterpret_cast<const char*>(data_opt->data()), static_cast<std::streamsize>(data_opt->size()));
        }
    }
    return true;
}

bool RomfsReader::MountToVfs(filesystem::VirtualFileSystem& vfs, const std::filesystem::path& staging_dir, std::string_view mount_prefix) const {
    if (!DumpToDirectory(staging_dir)) {
        return false;
    }
    return vfs.Mount(mount_prefix, staging_dir, /*read_only=*/true);
}

std::vector<u8> RomfsReader::BuildRomfs(const std::unordered_map<std::string, std::vector<u8>>& input_files) {
    // Collect all unique directories and files
    std::vector<std::string> sorted_paths;
    sorted_paths.reserve(input_files.size());
    for (const auto& [p, _] : input_files) {
        sorted_paths.push_back(p);
    }
    std::sort(sorted_paths.begin(), sorted_paths.end());

    // Construct RomFS in memory:
    // 1. RomfsHeader: 0x50 bytes
    // 2. Dir hash table: 4 bytes (1 bucket, offset 0)
    // 3. Dir meta table: root dir entry
    // 4. File hash table: 4 bytes (1 bucket, offset 0)
    // 5. File meta table: entries for all files
    // 6. Data section: concatenated file contents (16-byte aligned)

    constexpr u64 HEADER_SIZE = 0x50;
    constexpr u64 DIR_HASH_TABLE_SIZE = 4;
    constexpr u64 FILE_HASH_TABLE_SIZE = 4;

    // Root dir entry:
    // RomfsDirEntryHeader (24 bytes) + name length (0)
    constexpr u64 ROOT_DIR_META_SIZE = sizeof(RomfsDirEntryHeader);

    // Build file meta table and calculate data offsets
    std::vector<u8> file_meta;
    std::vector<u8> data_section;

    u32 first_file_entry_offset = sorted_paths.empty() ? 0xFFFFFFFF : 0;

    struct FileEntryInfo {
        std::string name;
        u64 data_offset;
        u64 data_size;
    };
    std::vector<FileEntryInfo> entries;

    for (const auto& path : sorted_paths) {
        const auto& content = input_files.at(path);
        // Align data section to 16 bytes
        while (data_section.size() % 16 != 0) {
            data_section.push_back(0);
        }
        u64 file_data_offset = data_section.size();
        data_section.insert(data_section.end(), content.begin(), content.end());

        entries.push_back(FileEntryInfo{
            .name = path,
            .data_offset = file_data_offset,
            .data_size = content.size()
        });
    }

    // Serialize file meta table
    std::vector<u32> file_entry_offsets;
    for (size_t i = 0; i < entries.size(); ++i) {
        file_entry_offsets.push_back(static_cast<u32>(file_meta.size()));
        const auto& e = entries[i];
        RomfsFileEntryHeader fhdr{
            .parent_dir_offset = 0,
            .sibling_offset = (i + 1 < entries.size()) ? 0 : 0xFFFFFFFF, // patched below
            .data_offset = e.data_offset,
            .data_size = e.data_size,
            .next_hash = 0xFFFFFFFF,
            .name_length = static_cast<u32>(e.name.size())
        };

        size_t start_off = file_meta.size();
        file_meta.resize(start_off + sizeof(RomfsFileEntryHeader) + e.name.size());
        std::memcpy(file_meta.data() + start_off, &fhdr, sizeof(RomfsFileEntryHeader));
        std::memcpy(file_meta.data() + start_off + sizeof(RomfsFileEntryHeader), e.name.data(), e.name.size());

        // Align entry to 4 bytes
        while (file_meta.size() % 4 != 0) {
            file_meta.push_back(0);
        }
    }

    // Patch sibling offsets in file meta
    for (size_t i = 0; i + 1 < entries.size(); ++i) {
        u32 cur_off = file_entry_offsets[i];
        u32 next_off = file_entry_offsets[i + 1];
        std::memcpy(file_meta.data() + cur_off + offsetof(RomfsFileEntryHeader, sibling_offset), &next_off, sizeof(u32));
    }

    // Build root dir meta
    std::vector<u8> dir_meta(ROOT_DIR_META_SIZE, 0);
    RomfsDirEntryHeader root_dir{
        .parent_offset = 0,
        .sibling_offset = 0xFFFFFFFF,
        .child_dir_offset = 0xFFFFFFFF,
        .child_file_offset = first_file_entry_offset,
        .next_hash = 0xFFFFFFFF,
        .name_length = 0
    };
    std::memcpy(dir_meta.data(), &root_dir, sizeof(RomfsDirEntryHeader));

    // Calculate layout offsets
    RomfsHeader hdr{};
    hdr.header_size = HEADER_SIZE;
    hdr.dir_hash_table_offset = HEADER_SIZE;
    hdr.dir_hash_table_size = DIR_HASH_TABLE_SIZE;

    hdr.dir_meta_table_offset = hdr.dir_hash_table_offset + hdr.dir_hash_table_size;
    hdr.dir_meta_table_size = dir_meta.size();

    hdr.file_hash_table_offset = hdr.dir_meta_table_offset + hdr.dir_meta_table_size;
    hdr.file_hash_table_size = FILE_HASH_TABLE_SIZE;

    hdr.file_meta_table_offset = hdr.file_hash_table_offset + hdr.file_hash_table_size;
    hdr.file_meta_table_size = file_meta.size();

    // Data offset aligned to 16 bytes
    u64 raw_data_start = hdr.file_meta_table_offset + hdr.file_meta_table_size;
    while (raw_data_start % 16 != 0) {
        raw_data_start++;
    }
    hdr.data_offset = raw_data_start;

    // Assemble image
    const size_t total_romfs_size = static_cast<size_t>(hdr.data_offset + data_section.size());
    std::vector<u8> romfs_image;
    romfs_image.resize(total_romfs_size, 0);

    if (sizeof(RomfsHeader) <= romfs_image.size()) {
        std::memcpy(romfs_image.data(), &hdr, sizeof(RomfsHeader));
    }

    u32 hash_zero = 0;
    if (hdr.dir_hash_table_offset + sizeof(u32) <= romfs_image.size()) {
        std::memcpy(romfs_image.data() + hdr.dir_hash_table_offset, &hash_zero, sizeof(u32));
    }
    if (!dir_meta.empty() && hdr.dir_meta_table_offset + dir_meta.size() <= romfs_image.size()) {
        std::memcpy(romfs_image.data() + hdr.dir_meta_table_offset, dir_meta.data(), dir_meta.size());
    }
    if (hdr.file_hash_table_offset + sizeof(u32) <= romfs_image.size()) {
        std::memcpy(romfs_image.data() + hdr.file_hash_table_offset, &hash_zero, sizeof(u32));
    }
    if (!file_meta.empty() && hdr.file_meta_table_offset + file_meta.size() <= romfs_image.size()) {
        std::memcpy(romfs_image.data() + hdr.file_meta_table_offset, file_meta.data(), file_meta.size());
    }
    if (!data_section.empty() && hdr.data_offset + data_section.size() <= romfs_image.size()) {
        std::memcpy(romfs_image.data() + hdr.data_offset, data_section.data(), data_section.size());
    }

    return romfs_image;
}

} // namespace nemu::core::loader
