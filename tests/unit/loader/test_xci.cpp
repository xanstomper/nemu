#include "core/loader/xci.hpp"
#include "core/loader/pfs0.hpp"
#include "core/types.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;

// Minimal HFS0 builder. Returns a complete HFS0 container in `out` with the
// given (name, data) payloads. This mirrors the Pfs0Archive layout: header +
// entries + string table, with file data offsets relative to the end of the
// header (data_offset_base_).
void BuildHfs0(const std::vector<std::pair<std::string, std::vector<u8>>>& payloads,
               std::vector<u8>& out) {
    struct Hfs0Header { u32 magic; u32 count; u32 strtab_size; u32 reserved; };
    struct Entry { u64 offset; u64 size; u32 str_offset; u32 hashed_size; u64 reserved2; u8 sha[32]; };

    const u32 count = static_cast<u32>(payloads.size());

    // Build string table.
    std::vector<u8> strtab;
    std::vector<u32> str_offsets(payloads.size());
    for (size_t i = 0; i < payloads.size(); ++i) {
        str_offsets[i] = static_cast<u32>(strtab.size());
        const auto& name = payloads[i].first;
        strtab.insert(strtab.end(), name.begin(), name.end());
        strtab.push_back(0);
    }

    const size_t header_size = sizeof(Hfs0Header) + count * sizeof(Entry);
    const size_t data_offset_base = header_size + strtab.size();
    // NOTE: Pfs0Archive::OpenFile computes abs_offset = data_offset_base_ +
    // entry.offset, where data_offset_base_ is the UNALIGNED end of the
    // container header. So data starts immediately at header_size + strtab.size()
    // (no alignment pad), and entry.offset is relative to that base.
    const size_t data_start = data_offset_base;

    out.assign(data_start, 0);
    for (size_t i = 0; i < payloads.size(); ++i) {
        out.insert(out.end(), payloads[i].second.begin(), payloads[i].second.end());
    }

    Hfs0Header hdr;
    hdr.magic = loader::Pfs0Archive::HFS0_MAGIC;
    hdr.count = count;
    hdr.strtab_size = static_cast<u32>(strtab.size());
    hdr.reserved = 0;
    std::memcpy(out.data(), &hdr, sizeof(hdr));

    size_t rel_offset = 0; // relative to data_start (matches parser semantics)
    for (size_t i = 0; i < payloads.size(); ++i) {
        Entry e{};
        e.offset = static_cast<u64>(rel_offset);
        e.size = static_cast<u64>(payloads[i].second.size());
        e.str_offset = str_offsets[i];
        e.hashed_size = static_cast<u32>(payloads[i].second.size());
        std::memcpy(out.data() + sizeof(hdr) + i * sizeof(Entry), &e, sizeof(e));
        rel_offset += payloads[i].second.size();
    }

    std::memcpy(out.data() + sizeof(hdr) + count * sizeof(Entry), strtab.data(), strtab.size());
}

// Build a full XCI: 0x200-byte header + HFS0 partition table + "normal"
// partition body that is itself an HFS0 with game payload entries.
std::vector<u8> BuildXci() {
    // Inner payload list for the "normal" partition: a fake NSP/PFS0 blob.
    std::vector<u8> fake_nsp = {'N','S','P','0', 1,2,3,4,5,6,7,8,9,10,11,12};
    std::vector<u8> normal_body;
    BuildHfs0({ {"mygame.nsp", fake_nsp} }, normal_body);

    // Outer partition table (HFS0) with "normal" + "secure" + "logo".
    std::vector<u8> part_table;
    BuildHfs0({ {"normal", normal_body},
                {"secure", std::vector<u8>(64, 0xEE)},
                {"logo", std::vector<u8>(32, 0xFF)} }, part_table);

    std::vector<u8> xci(0x200, 0);
    xci.insert(xci.end(), part_table.begin(), part_table.end());
    return xci;
}

int main() {
    std::cout << "[Test: XCI Cartridge Reader]" << std::endl;

    auto xci = BuildXci();

    // 1. Detection.
    NEMU_TEST_ASSERT(loader::XciArchive::IsXci(xci), "Synthetic XCI must be detected");

    // 2. Reject garbage / too-small buffers.
    std::vector<u8> small(0x10, 0);
    NEMU_TEST_ASSERT(!loader::XciArchive::IsXci(small), "Small buffer must not be XCI");
    std::vector<u8> garbage(0x300, 0xAB);
    NEMU_TEST_ASSERT(!loader::XciArchive::IsXci(garbage), "Garbage must not be XCI");

    // 3. Parse and check partitions.
    loader::XciArchive xci_archive;
    NEMU_TEST_ASSERT(xci_archive.Initialize(xci), "XCI should initialize");
    const auto& parts = xci_archive.PartitionNames();
    bool has_normal = false;
    for (const auto& p : parts) {
        if (p == "normal") has_normal = true;
    }
    NEMU_TEST_ASSERT(has_normal, "XCI must contain a 'normal' partition");

    // 4. Unpack game payloads -> should find 'mygame.nsp' from normal partition.
    std::vector<loader::XciPayload> payloads;
    NEMU_TEST_ASSERT(xci_archive.UnpackGame(payloads), "UnpackGame should succeed");
    NEMU_TEST_ASSERT(payloads.size() == 1, "Should unpack exactly one game payload");
    NEMU_TEST_ASSERT(payloads[0].name == "mygame.nsp", "Payload name should survive");
    NEMU_TEST_ASSERT(payloads[0].data.size() == 16, "Payload size must match");
    NEMU_TEST_ASSERT(payloads[0].data[0] == 'N' && payloads[0].data[1] == 'S',
                     "Payload content must round-trip");
    // Offset must point into the original XCI buffer.
    NEMU_TEST_ASSERT(payloads[0].offset > 0x200 && payloads[0].offset < xci.size(),
                     "Payload offset must be within XCI buffer");

    // 5. Payload span must alias the original buffer (no copy).
    NEMU_TEST_ASSERT(payloads[0].data.data() >= xci.data() &&
                     payloads[0].data.data() < xci.data() + xci.size(),
                     "Payload data must alias the original XCI buffer (no copy)");

    std::cout << "[Test: XCI Cartridge Reader] ALL PASSED" << std::endl;
    return 0;
}