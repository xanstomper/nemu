#include "pfs0.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::loader {

#pragma pack(push, 1)
struct Pfs0Header {
    u32 magic;
    u32 file_count;
    u32 string_table_size;
    u32 reserved;
};

struct RawPfs0FileEntry {
    u64 offset;
    u64 size;
    u32 string_table_offset;
    u32 reserved;
};

struct RawHfs0FileEntry {
    u64 offset;
    u64 size;
    u32 string_table_offset;
    u32 hashed_size;
    u64 reserved;
    u8  sha256[32];
};
#pragma pack(pop)

bool Pfs0Archive::Initialize(std::span<const u8> data) {
    if (data.size() < sizeof(Pfs0Header)) {
        return false;
    }

    raw_data_ = data;
    files_.clear();

    Pfs0Header hdr{};
    std::memcpy(&hdr, data.data(), sizeof(Pfs0Header));

    if (hdr.magic == PFS0_MAGIC) {
        is_hfs0_ = false;
    } else if (hdr.magic == HFS0_MAGIC) {
        is_hfs0_ = true;
    } else {
        return false;
    }

    const size_t entry_size = is_hfs0_ ? sizeof(RawHfs0FileEntry) : sizeof(RawPfs0FileEntry);
    const size_t entries_total_size = hdr.file_count * entry_size;
    const size_t string_table_start = sizeof(Pfs0Header) + entries_total_size;
    const size_t header_total_size = string_table_start + hdr.string_table_size;

    if (data.size() < header_total_size) {
        return false;
    }

    data_offset_base_ = header_total_size;
    const u8* str_table = data.data() + string_table_start;

    files_.reserve(hdr.file_count);

    for (u32 i = 0; i < hdr.file_count; ++i) {
        u64 file_offset = 0;
        u64 file_size = 0;
        u32 str_offset = 0;

        if (is_hfs0_) {
            RawHfs0FileEntry entry{};
            std::memcpy(&entry, data.data() + sizeof(Pfs0Header) + i * sizeof(RawHfs0FileEntry), sizeof(entry));
            file_offset = entry.offset;
            file_size = entry.size;
            str_offset = entry.string_table_offset;
        } else {
            RawPfs0FileEntry entry{};
            std::memcpy(&entry, data.data() + sizeof(Pfs0Header) + i * sizeof(RawPfs0FileEntry), sizeof(entry));
            file_offset = entry.offset;
            file_size = entry.size;
            str_offset = entry.string_table_offset;
        }

        std::string name;
        if (str_offset < hdr.string_table_size) {
            const char* name_ptr = reinterpret_cast<const char*>(str_table + str_offset);
            size_t max_len = hdr.string_table_size - str_offset;
            size_t len = strnlen(name_ptr, max_len);
            name.assign(name_ptr, len);
        }

        files_.push_back(Pfs0FileEntry{
            .name = std::move(name),
            .offset = file_offset,
            .size = file_size
        });
    }

    return true;
}

bool Pfs0Archive::HasFile(std::string_view name) const {
    for (const auto& f : files_) {
        if (f.name == name) return true;
    }
    return false;
}

std::optional<std::span<const u8>> Pfs0Archive::OpenFile(std::string_view name) const {
    for (size_t i = 0; i < files_.size(); ++i) {
        if (files_[i].name == name) {
            return OpenFile(i);
        }
    }
    return std::nullopt;
}

std::optional<std::span<const u8>> Pfs0Archive::OpenFile(size_t index) const {
    if (index >= files_.size()) {
        return std::nullopt;
    }

    const auto& f = files_[index];
    const u64 abs_offset = data_offset_base_ + f.offset;
    if (abs_offset + f.size > raw_data_.size()) {
        return std::nullopt;
    }

    return std::span<const u8>(raw_data_.data() + abs_offset, static_cast<size_t>(f.size));
}

} // namespace nemu::core::loader
