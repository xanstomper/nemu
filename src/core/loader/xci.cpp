#include "xci.hpp"
#include "pfs0.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::loader {

// Layout invariants (stable, well-documented across Switch tooling):
//   * XCI header is 0x200 bytes.
//   * The partition table is an HFS0 located at offset 0x200.
//   * Partition names include "normal" (game content), "secure", "update",
//     "logo". Only the "normal" partition holds the playable game payloads.
constexpr size_t kXciHeaderSize = 0x200;

bool XciArchive::IsXci(std::span<const u8> data) {
    if (data.size() < kXciHeaderSize + 4) {
        return false;
    }
    u32 magic = 0;
    std::memcpy(&magic, data.data() + kXciHeaderSize, 4);
    return magic == Pfs0Archive::HFS0_MAGIC;
}

bool XciArchive::Initialize(std::span<const u8> data) {
    raw_data_ = data;
    partition_names_.clear();
    valid_ = false;

    if (!IsXci(data)) {
        return false;
    }

    // The partition table is an HFS0 at offset 0x200 within the XCI.
    std::span<const u8> pt(data.data() + kXciHeaderSize, data.size() - kXciHeaderSize);
    Pfs0Archive part_table;
    if (!part_table.Initialize(pt) || !part_table.IsHfs0()) {
        NEMU_LOG_ERROR("XCI", "XCI partition table at +0x200 is not a valid HFS0");
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
    if (!valid_) {
        return false;
    }
    if (raw_data_.size() < kXciHeaderSize + 4) {
        return false;
    }

    std::span<const u8> pt(raw_data_.data() + kXciHeaderSize, raw_data_.size() - kXciHeaderSize);
    Pfs0Archive part_table;
    if (!part_table.Initialize(pt) || !part_table.IsHfs0()) {
        return false;
    }

    // Find the "normal" partition that holds the game.
    auto normal = part_table.OpenFile("normal");
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
    NEMU_LOG_INFO("XCI", "Using 'normal' partition ({} bytes)", normal->size());

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
                // Absolute offset into the original XCI buffer for diagnostics.
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