// Diagnostic: run NEMU's own NcaReader over a real cart and report exactly how
// far it gets on the Program NCA (header decrypt -> section 0 -> ExeFS).
//
//   g++ -std=c++20 -I src tools/nca_probe.cpp -Lbuild/lib -lNemu.Loader \
//       -lNemu.Crypto -lNemu.Platform -lNemu.FileSystem -lNemu.Memory \
//       -lNemu.Cpu -lzstd -lpthread -o /tmp/nca_probe

#include "core/loader/xci.hpp"
#include "core/loader/pfs0.hpp"
#include "core/loader/nca.hpp"
#include "core/crypto/key_store.hpp"
#include "core/types.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace nemu;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <cart.xci> [keys_dir]\n", argv[0]);
        return 2;
    }

    core::crypto::KeyStore ks;
    std::string keys_dir = (argc > 2) ? argv[2] : "games";
    ks.LoadFromFile(keys_dir + "/prod.keys");
    ks.LoadFromFile(keys_dir + "/title.keys");
    std::printf("keystore: count=%zu\n", ks.Count());

    std::ifstream file(argv[1], std::ios::binary);
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

    core::loader::XciArchive cart;
    if (!cart.Initialize(data)) {
        std::printf("cart init FAILED\n");
        return 1;
    }
    std::vector<core::loader::XciPayload> payloads;
    if (!cart.UnpackGame(payloads)) {
        std::printf("unpack FAILED\n");
        return 1;
    }

    size_t tried = 0;
    for (const auto& p : payloads) {
        if (p.name.find(".cnmt.") != std::string::npos) {
            continue;
        }
        if (p.data.size() < 0x1000 ||
            (p.name.size() < 4 || p.name.compare(p.name.size() - 4, 4, ".nca") != 0)) {
            continue;
        }
        if (++tried > 6) {
            break;
        }

        core::loader::NcaReader nca;
        const bool ok = nca.Initialize(p.data, &ks);
        std::printf("\n--- %s (%zu B) init=%d ---\n", p.name.c_str(), p.data.size(),
                    static_cast<int>(ok));
        if (!ok) {
            continue;
        }
        std::printf("  title_id=0x%016llX content_type=%d keygen=%d kaek_idx=%d\n",
                    static_cast<unsigned long long>(nca.GetTitleId()),
                    static_cast<int>(nca.GetContentType()),
                    static_cast<int>(nca.GetKeyGeneration()),
                    static_cast<int>(0));
        for (u32 s = 0; s < 4; ++s) {
            if (nca.HasSection(s)) {
                auto info = nca.GetSectionInfo(s);
                std::printf("  section %u: offset=0x%llX size=0x%llX\n", s,
                            static_cast<unsigned long long>(info->offset),
                            static_cast<unsigned long long>(info->size));
            }
        }

        auto sec0 = nca.ExtractSection(0, &ks);
        if (!sec0) {
            std::printf("  ExtractSection(0) -> FAILED (no key resolved)\n");
            continue;
        }
        std::printf("  ExtractSection(0) -> %zu bytes, first32: ", sec0->size());
        for (size_t i = 0; i < 32 && i < sec0->size(); ++i) {
            std::printf("%02x", (*sec0)[i]);
        }
        std::printf("\n");
        std::printf("  as ASCII: \"");
        for (size_t i = 0; i < 8 && i < sec0->size(); ++i) {
            const u8 c = (*sec0)[i];
            std::printf("%c", (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
        }
        std::printf("\"\n");

        core::loader::Pfs0Archive exefs;
        if (!exefs.Initialize(*sec0)) {
            std::printf("  => ExeFS PFS0 parse FAILED (section decrypt or layout wrong)\n");
        } else {
            std::printf("  => ExeFS PFS0 OK, %zu files:", exefs.FileCount());
            for (const auto& f : exefs.GetFiles()) {
                std::printf(" %s(%llu)", f.name.c_str(),
                            static_cast<unsigned long long>(f.size));
            }
            std::printf("\n");
        }
    }
    return 0;
}
