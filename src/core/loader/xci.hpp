#pragma once

#include "core/types.hpp"
#include <span>
#include <string>
#include <vector>
#include <optional>

namespace nemu::core::loader {

/// A single unpacked payload extracted from a cartridge image (.xci).
/// The payload is a view into the original XCI buffer (no copy).
struct XciPayload {
    std::string name;   ///< Original entry name (e.g. "main", "845F5B5DE2E4...nca", "update.nsp")
    std::span<const u8> data;

    /// Offset within the original XCI buffer (0 if a copy, -1 if none).
    size_t offset{0};
    bool is_copy{false};
};

/// Cartridge-image (XCI) reader.
///
/// Format: a 0x200-byte XCI header followed by an HFS0 partition table at
/// offset 0x200. Partitions include "normal" (the game), "secure", "update",
/// "logo". The "normal" partition is itself an HFS0 whose entries are the
/// game's NSP files / NCAs.
///
/// This reader reuses Pfs0Archive to parse both HFS0 levels and exposes the
/// unpacked game payloads as spans into the caller's buffer — no copies.
class XciArchive {
public:
    /// Detect an XCI by the HFS0 partition-table magic at offset 0x200.
    static bool IsXci(std::span<const u8> data);

    /// Parse the cartridge. Returns false if it isn't a valid XCI.
    bool Initialize(std::span<const u8> data);

    /// Unpack the game payloads from the "normal" partition into `payloads`.
    /// Each payload is a span into the original buffer (no data copied).
    /// payloads are appended to the caller-supplied vector so callers can
    /// chain multiple XCIs if desired.
    bool UnpackGame(std::vector<XciPayload>& payloads) const;

    /// List the top-level partition names found in the XCI partition table.
    [[nodiscard]] const std::vector<std::string>& PartitionNames() const noexcept { return partition_names_; }

private:
    std::span<const u8> raw_data_;
    std::vector<std::string> partition_names_;
    size_t partition_table_offset_{0};  ///< detected HFS0 offset (0x200/0xF000/0x0)
    bool valid_{false};
};

} // namespace nemu::core::loader