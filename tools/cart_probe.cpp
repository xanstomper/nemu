// Ground-truth XCI probe: runs NEMU's real loader against a cartridge image
// and prints what the loader actually sees. Companion to xci_probe.py, which
// parses the container independently so the two can be diffed.
//
//   g++ -std=c++20 -I src tools/cart_probe.cpp -Lbuild/lib \
//       -lNemu.Loader -lNemu.Crypto -lNemu.Platform -o /tmp/cart_probe

#include "core/loader/xci.hpp"
#include "core/loader/pfs0.hpp"
#include "core/loader/nca.hpp"
#include "core/types.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace nemu;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <cart.xci> [max_entries]\n", argv[0]);
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!file) {
        std::fprintf(stderr, "short read\n");
        return 2;
    }
    std::printf("cart: %s  %zu bytes (0x%zX)\n", argv[1], size, size);

    std::span<const u8> view(data);
    core::loader::XciArchive cart;
    if (!cart.Initialize(view)) {
        std::printf("RESULT: XciArchive::Initialize FAILED\n");
        return 1;
    }

    std::printf("partitions:");
    for (const auto& n : cart.PartitionNames()) {
        std::printf(" %s", n.c_str());
    }
    std::printf("\n");

    std::vector<core::loader::XciPayload> payloads;
    if (!cart.UnpackGame(payloads)) {
        std::printf("RESULT: UnpackGame FAILED (no payloads)\n");
        return 1;
    }

    const size_t limit = (argc > 2) ? static_cast<size_t>(std::atol(argv[2])) : 12;
    size_t nca = 0;
    size_t cnmt = 0;
    size_t tik = 0;
    size_t cert = 0;
    u64 largest = 0;
    std::string largest_name;
    for (const auto& p : payloads) {
        const bool is_cnmt = p.name.find(".cnmt.") != std::string::npos;
        if (is_cnmt) {
            ++cnmt;
            continue;
        }
        if (p.name.size() > 4 && p.name.compare(p.name.size() - 4, 4, ".nca") == 0) {
            ++nca;
            if (p.data.size() > largest) {
                largest = p.data.size();
                largest_name = p.name;
            }
        } else if (p.name.size() > 4 && p.name.compare(p.name.size() - 4, 4, ".tik") == 0) {
            ++tik;
        } else if (p.name.size() > 5 && p.name.compare(p.name.size() - 5, 5, ".cert") == 0) {
            ++cert;
        }
    }

    std::printf("payloads: %zu total (%zu .nca, %zu .cnmt.nca, %zu .tik, %zu .cert)\n",
                payloads.size(), nca, cnmt, tik, cert);
    std::printf("largest program NCA: %s  %llu bytes\n", largest_name.c_str(),
                static_cast<unsigned long long>(largest));

    const u32 magic = core::loader::NcaReader::NCA3_MAGIC;
    std::printf("expected unencrypted NCA magic: 0x%08X\n", magic);

    std::size_t shown = 0;
    for (const auto& p : payloads) {
        if (shown++ >= limit) {
            break;
        }
        const u32 head = (p.data.size() >= 4)
            ? static_cast<u32>(p.data[0]) | (static_cast<u32>(p.data[1]) << 8) |
              (static_cast<u32>(p.data[2]) << 16) | (static_cast<u32>(p.data[3]) << 24)
            : 0;
        const u32 magic200 = (p.data.size() >= 0x204)
            ? static_cast<u32>(p.data[0x200]) | (static_cast<u32>(p.data[0x201]) << 8) |
              (static_cast<u32>(p.data[0x202]) << 16) | (static_cast<u32>(p.data[0x203]) << 24)
            : 0;
        std::printf("  %-44s %10zu B @0x%08zX  magic0=0x%08X magic@0x200=0x%08X\n",
                    p.name.c_str(), p.data.size(), p.offset, head, magic200);
    }

    return 0;
}
