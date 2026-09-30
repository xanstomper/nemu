#include "xci.hpp"
#include "pfs0.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace nemu::core::loader {

// Layout invariants (stable, well-documented across Switch tooling):
//   * XCI header is 0x200 bytes.
//   * The partition table is an HFS0 whose offset depends on the cart layout:
//       - 0x200  : headerless / minimal XCI (partition table right after header)
//       - 0xF000 : standard XCI (0xF000 partition offset)
//       - 0x00   : partition table at the very start
//   * Partition names include "normal" (game content), "secure", "update",
//     "logo". Only the "normal" partition holds the playable game payloads.
constexpr size_t kXciHeaderSize = 0x200;
constexpr size_t kXciPartitionOffset  = 0xF000;
constexpr size_t kXciPartitionOffset2 = 0x0000;

/// Detect the absolute file offset of the HFS0 partition table inside an XCI
/// by scanning the known valid offsets. Returns kInvalidOffset if none match.
/// This is why IsXci() must not hardcode a single offset (real carts use 0xF000).
static constexpr size_t kInvalidOffset = static_cast<size_t>(-1);
static size_t DetectPartitionTableOffset(std::span<const u8> data) {
    for (const size_t off : {kXciHeaderSize, kXciPartitionOffset, kXciPartitionOffset2}) {
        if (data.size() < off + 4) continue;
        u32 magic = 0;
        std::memcpy(&magic, data.data() + off, 4);
        if (magic == Pfs0Archive::HFS0_MAGIC) {
            return off;
        }
    }
    return kInvalidOffset;
}

bool XciArchive::IsXci(std::span<const u8> data) {
    return DetectPartitionTableOffset(data) != kInvalidOffset;
}

bool XciArchive::Initialize(std::span<const u8> data) {
    raw_data_ = data;
    partition_names_.clear();
    valid_ = false;

    const size_t pt_off = DetectPartitionTableOffset(data);
    if (pt_off == kInvalidOffset) {
        return false;
    }
    partition_table_offset_ = pt_off;

    // The partition table is an HFS0 at the detected offset within the XCI.
    std::span<const u8> pt(data.data() + pt_off, data.size() - pt_off);
    Pfs0Archive part_table;
    if (!part_table.Initialize(pt) || !part_table.IsHfs0()) {
        NEMU_LOG_ERROR("XCI", "XCI partition table at +0x{:X} is not a valid HFS0", pt_off);
        return false;
    }

    for (const auto& f : part_table.GetFiles()) {
        partition_names_.push_back(f.name);
        NEMU_LOG_INFO("XCI", "Partition '{}' size=0x{:X} offset=0x{:X} container_header=0x{:X}",
                      f.name, f.size, f.offset, f.hashed_size);
    }

    valid_ = true;
    return true;
}

bool XciArchive::UnpackGame(std::vector<XciPayload>& payloads) const {
    if (!valid_ || partition_table_offset_ == static_cast<size_t>(-1)) {
        return false;
    }
    const size_t ptoff = partition_table_offset_;
    if (raw_data_.size() < ptoff + 4) {
        return false;
    }

    std::span<const u8> pt(raw_data_.data() + ptoff, raw_data_.size() - ptoff);
    Pfs0Archive part_table;
    if (!part_table.Initialize(pt) || !part_table.IsHfs0()) {
        return false;
    }

    // Which partition actually carries the NCAs is not fixed by the format.
    // Retail dumps put the base game in "normal", but plenty of carts ship an
    // empty "normal" stub and keep everything in "secure" and/or "update".
    // Walk the content partitions in priority order and harvest all of them.
    static constexpr std::string_view kContentPartitions[] = {
        "normal", "secure", "update",
    };

    // A partition entry is the *body* of a nested container; the container
    // header sits at `entry.offset` from the data region origin, which for XCI
    // partitions is the entry origin itself. Resolve both candidates.
    auto collect_from = [&](std::span<const u8> partition, std::string_view label,
                            std::vector<XciPayload>& out) -> size_t {
        if (partition.size() < sizeof(u32)) {
            return 0;
        }
        Pfs0Archive fs;
        if (!fs.Initialize(partition)) {
            return 0;
        }
        size_t added = 0;
        for (const auto& f : fs.GetFiles()) {
            auto entry = fs.OpenFile(f.name);
            if (!entry) {
                continue;
            }
            out.push_back(XciPayload{
                .name = f.name,
                .data = *entry,
                .offset = static_cast<size_t>((*entry).data() - raw_data_.data()),
                .is_copy = false,
            });
            ++added;
            NEMU_LOG_INFO("XCI", "  payload[{}] '{}' {} bytes @0x{:X}", label, f.name,
                          f.size, out.back().offset);
        }
        return added;
    };

    size_t total = 0;
    for (const std::string_view name : kContentPartitions) {
        const Pfs0FileEntry* entry = nullptr;
        for (const auto& f : part_table.GetFiles()) {
            if (f.name == name) {
                entry = &f;
                break;
            }
        }
        if (!entry) {
            continue;
        }
        auto body = part_table.OpenFile(entry->name);
        if (!body) {
            NEMU_LOG_WARN("XCI", "Partition '{}' has an out-of-range extent", name);
            continue;
        }
        const size_t added = collect_from(*body, name, payloads);
        if (added > 0) {
            NEMU_LOG_INFO("XCI", "Partition '{}' contributed {} payloads", name, added);
            total += added;
        }
    }

    if (total == 0) {
        // Last resort: a few candidates may hide the content container outside
        // the named partitions. Probe only 0x200-aligned offsets near the start
        // of the cart (where partition tables live) instead of sweeping hundreds
        // of megabytes sector by sector.
        static constexpr size_t kProbeWindow = 0x400000; // 4 MiB past the header
        const size_t scan_max = std::min(raw_data_.size(), kProbeWindow);
        for (size_t off = 0; off + sizeof(u32) <= scan_max; off += 0x200) {
            const u8* p = raw_data_.data() + off;
            const bool is_container = (p[0] == 'H' || p[0] == 'P') &&
                                      p[1] == 'F' && p[2] == 'S' && p[3] == '0';
            if (!is_container) {
                continue;
            }
            Pfs0Archive probe;
            if (!probe.Initialize(std::span<const u8>(p, raw_data_.size() - off))) {
                continue;
            }
            bool looks_game = false;
            for (const auto& f : probe.GetFiles()) {
                if (f.name.ends_with(".nca") || f.name.ends_with(".nsp") ||
                    f.name.ends_with(".tik") || f.name.ends_with(".cert")) {
                    looks_game = true;
                    break;
                }
            }
            if (!looks_game) {
                continue;
            }
            total += collect_from(std::span<const u8>(p, raw_data_.size() - off),
                                  "scan", payloads);
            if (total > 0) {
                break;
            }
        }
    }

    if (total == 0) {
        NEMU_LOG_WARN("XCI",
                      "XCI parsed ({} partitions) but no game content container found; "
                      "nothing to boot", partition_names_.size());
        return false;
    }

    NEMU_LOG_INFO("XCI", "Unpacked {} payloads from {} partitions", total,
                  partition_names_.size());
    return true;
}

} // namespace nemu::core::loader