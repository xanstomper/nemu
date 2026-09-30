#include "xci.hpp"
#include "pfs0.hpp"
#include "platform/logger.hpp"
#include <cstring>

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
        NEMU_LOG_INFO("XCI", "Partition table entry: '{}' ({}) offset=0x{:X} size=0x{:X}",
                      f.name, f.size, f.offset, f.size);
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

    // Locate the "normal" partition that holds the game.
    std::optional<std::span<const u8>> normal = part_table.OpenFile("normal");
    if (!normal) {
        // Some small / unusual carts expose the game directly in a partition
        // named differently; fall back to the largest non-system partition.
        const Pfs0FileEntry* largest = nullptr;
        for (const auto& f : part_table.GetFiles()) {
            if (f.name == "secure" || f.name == "logo" || f.name == "update") continue;
            if (!largest || f.size > largest->size) largest = &f;
        }
        if (largest) normal = part_table.OpenFile(largest->name);
    }
    if (!normal) {
        NEMU_LOG_WARN("XCI", "No 'normal' partition found in XCI; nothing to boot");
        return false;
    }

    // The "normal" partition is itself an HFS0 whose entries are the game's
    // NSP files / NCAs. Walk it and expose every payload as a span.
    Pfs0Archive normal_fs;
    if (normal_fs.Initialize(*normal)) {
        for (const auto& f : normal_fs.GetFiles()) {
            auto entry = normal_fs.OpenFile(f.name);
            if (!entry) continue;
            payloads.push_back(XciPayload{
                .name = f.name,
                .data = *entry,
                .offset = static_cast<size_t>((*entry).data() - raw_data_.data()),
                .is_copy = false,
            });
            NEMU_LOG_INFO("XCI", "  game payload: '{}' ({} bytes) @0x{:X}", f.name, f.size,
                          payloads.back().offset);
        }
    } else {
        // Some carts store the PFS0/NSP directly as the normal partition body.
        payloads.push_back(XciPayload{
            .name = "normal",
            .data = *normal,
            .offset = static_cast<size_t>(normal->data() - raw_data_.data()),
            .is_copy = false,
        });
        NEMU_LOG_INFO("XCI", "  game payload: 'normal' ({} bytes)", normal->size());
    }

    return !payloads.empty();
}

} // namespace nemu::core::loader