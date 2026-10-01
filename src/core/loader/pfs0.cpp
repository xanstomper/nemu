#include "pfs0.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace nemu::core::loader {

#pragma pack(push, 1)
struct Pfs0Header {
    u32 magic;
    u32 file_count;
    u32 string_table_size;
    u32 reserved;
};
#pragma pack(pop)

namespace {

/// Round `value` up to the next 0x200 boundary (container data alignment).
constexpr u64 AlignSector(u64 value) {
    return (value + 0x1FF) & ~static_cast<u64>(0x1FF);
}

/// HFS0/HFS1 stores a u32 `header_size` immediately after the string table.
/// Retail carts frequently leave garbage there (the data region already starts
/// at the aligned header end), so accept the stored value only when it is
/// plausible and fall back to the canonical aligned header end otherwise.
constexpr u64 kMaxPlausibleHeaderSize = 0x20000;

u64 ResolveHeaderSize(const u8* data, size_t avail, u64 header_end) {
    u32 stored = 0;
    if (header_end + sizeof(stored) <= avail) {
        std::memcpy(&stored, data + header_end, sizeof(stored));
    }
    const u64 candidate = stored;
    const bool plausible = candidate >= header_end + sizeof(stored) &&
                           candidate <= kMaxPlausibleHeaderSize &&
                           (candidate & 0x1FF) == 0;
    return plausible ? candidate : AlignSector(header_end);
}

} // namespace

bool Pfs0Archive::TryParseEntries(std::span<const u8> data, u32 stride,
                                  std::vector<Pfs0FileEntry>& out) const {
    if (stride < sizeof(u64) * 2 + sizeof(u32) * 2) {
        return false;
    }

    Pfs0Header hdr{};
    std::memcpy(&hdr, data.data(), sizeof(Pfs0Header));

    const u64 string_table_start = sizeof(Pfs0Header) +
                                   static_cast<u64>(hdr.file_count) * stride;
    const u64 header_end = string_table_start + hdr.string_table_size;
    if (string_table_start > data.size() || header_end + sizeof(u32) > data.size()) {
        return false;
    }

    const u8* str_table = data.data() + string_table_start;
    // PFS0 (inner ExeFS/NSP containers): file data begins EXACTLY after the
    // string table (hactool pfs0_get_header_size). No alignment, no stored
    // size field. HFS0 (XCI partitions): u32 header_size follows, garbage on
    // retail carts -> fall back to the 0x200-aligned end.
    const u64 data_base = is_hfs0_
        ? ResolveHeaderSize(data.data(), data.size(), header_end)
        : header_end;
    if (data_base > data.size()) {
        return false;
    }
    const u64 table_limit = data.size() - data_base;

    std::vector<Pfs0FileEntry> parsed;
    parsed.reserve(hdr.file_count);

    for (u32 i = 0; i < hdr.file_count; ++i) {
        const u8* entry = data.data() + sizeof(Pfs0Header) + static_cast<u64>(i) * stride;
        u64 file_offset = 0;
        u64 file_size = 0;
        u32 str_offset = 0;
        u32 hashed = 0;
        std::memcpy(&file_offset, entry, sizeof(u64));
        std::memcpy(&file_size, entry + sizeof(u64), sizeof(u64));
        std::memcpy(&str_offset, entry + sizeof(u64) * 2, sizeof(u32));
        std::memcpy(&hashed, entry + sizeof(u64) * 2 + sizeof(u32), sizeof(u32));

        // A name offset outside the string table means the stride is wrong.
        if (str_offset >= hdr.string_table_size) {
            return false;
        }
        // Names in a valid container start with printable ASCII.
        const u8 first = str_table[str_offset];
        if (first < 0x20 || first > 0x7E) {
            return false;
        }
        // Extents must lie inside the container.
        if (file_offset > data.size() || file_size > data.size() ||
            file_offset + file_size > table_limit) {
            return false;
        }

        const char* name_ptr = reinterpret_cast<const char*>(str_table + str_offset);
        const size_t max_len = hdr.string_table_size - str_offset;
        const size_t len = strnlen(name_ptr, max_len);
        parsed.push_back(Pfs0FileEntry{
            .name = std::string(name_ptr, len),
            .offset = file_offset,
            .size = file_size,
            .hashed_size = hashed,
        });
    }

    out = std::move(parsed);
    return true;
}

bool Pfs0Archive::Initialize(std::span<const u8> data) {
    if (data.size() < sizeof(Pfs0Header)) {
        return false;
    }

    raw_data_ = data;
    files_.clear();
    header_size_ = 0;
    entry_stride_ = 0;

    Pfs0Header hdr{};
    std::memcpy(&hdr, data.data(), sizeof(Pfs0Header));

    if (hdr.magic == PFS0_MAGIC) {
        is_hfs0_ = false;
    } else if (hdr.magic == HFS0_MAGIC) {
        is_hfs0_ = true;
    } else {
        return false;
    }

    // The entry stride is not encoded anywhere, and retail carts disagree on
    // it (plain HFS0 uses 0x18, hash-bearing variants use 0x38/0x40). Probe the
    // candidates and keep the first whose name table and extents validate.
    static constexpr u32 kStrides[] = {
        kEntrySizeHfs0, kEntrySizeHfs1, kEntrySizeHashed,
    };

    for (const u32 stride : kStrides) {
        std::vector<Pfs0FileEntry> parsed;
        if (TryParseEntries(data, stride, parsed)) {
            files_ = std::move(parsed);
            entry_stride_ = stride;
            break;
        }
    }

    if (entry_stride_ == 0) {
        return false;
    }

    const u64 string_table_start = sizeof(Pfs0Header) +
                                   static_cast<u64>(hdr.file_count) * entry_stride_;
    const u64 header_end = string_table_start + hdr.string_table_size;
    // PFS0: data starts exactly at header end (hactool rule). HFS0: honor the
    // stored u32 header_size when plausible, else the aligned end.
    header_size_ = is_hfs0_
        ? ResolveHeaderSize(data.data(), data.size(), header_end)
        : header_end;

    // File data begins at the container's header size.
    data_offset_base_ = header_size_;

    NEMU_LOG_INFO("Loader",
                  "PFS0/HFS0: {} files, entry stride 0x{:X}, string table 0x{:X}, "
                  "data base 0x{:X}",
                  hdr.file_count, entry_stride_, hdr.string_table_size,
                  static_cast<u64>(data_offset_base_));

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
