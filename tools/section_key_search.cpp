// Exhaustive, oracle-driven search for the NCA section key.
//
// Ground truth for this cart (decrypted header of the 159 MB base-game NCA):
//   0x204 crypto byte = 0        -> sections ARE encrypted (flags=1)
//   0x206 key_generation = 2, 0x207 kaek_index = 0
//   0x240 section entries: sec0 media 0x3CEA0..0x4F980 (file 0x79D4000..0x9F30000)
//   0x280 section_ctrs[4]   <-- present and non-zero; NEMU never reads these
//   0x2C0 key_area[4]       <-- NEMU reads the key area at 0x300 instead
//
// For a game card the Key-Area-Key is derived from the gamecard header (the
// 0xF000-byte XCI header) plus a keyblob master key -- there are no title keys.
// This sweeps every plausible derivation against the definitive oracle: the
// ExeFS section must decrypt to the magic "PFS0".
//
//   g++ -std=c++20 -O2 -I src tools/section_key_search.cpp -Lbuild/lib \
//       -lNemu.Loader -lNemu.Crypto -lNemu.Platform -lNemu.FileSystem \
//       -lNemu.Memory -lNemu.Cpu -lzstd -lpthread -o /tmp/sec_key

#include "core/loader/xci.hpp"
#include "core/crypto/aes.hpp"
#include "core/crypto/key_store.hpp"
#include "core/types.hpp"

#include <array>
#include <cstdint>
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
    const std::string keys_dir = (argc > 2) ? argv[2] : "games";

    core::crypto::KeyStore ks;
    ks.LoadFromFile(keys_dir + "/prod.keys");
    ks.LoadFromFile(keys_dir + "/title.keys");

    auto hk = ks.GetHeaderKey();
    if (!hk || hk->size() < 32) {
        std::fprintf(stderr, "no header_key\n");
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    file.seekg(0, std::ios::end);
    const auto fsize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> rom(fsize);
    file.read(reinterpret_cast<char*>(rom.data()), static_cast<std::streamsize>(fsize));

    std::vector<u8> gh(0x200);
    std::memcpy(gh.data(), rom.data(), 0x200);

    core::loader::XciArchive cart;
    if (!cart.Initialize(rom)) {
        return 1;
    }
    std::vector<core::loader::XciPayload> payloads;
    if (!cart.UnpackGame(payloads)) {
        return 1;
    }

    const core::loader::XciPayload* target = nullptr;
    for (const auto& p : payloads) {
        if (p.name.find(".cnmt.") != std::string::npos || p.name.find(".nca") == std::string::npos) {
            continue;
        }
        if (p.data.size() < 0x10000) {
            continue;
        }
        if (!target || p.data.size() > target->data.size()) {
            target = &p;
        }
    }
    if (!target) {
        std::fprintf(stderr, "no program NCA\n");
        return 1;
    }
    std::printf("target: %s (%zu bytes)\n", target->name.c_str(), target->data.size());

    std::vector<u8> hdr(0xC00, 0);
    core::crypto::Aes128 k1(std::span<const u8, 16>(hk->data(), 16));
    core::crypto::Aes128 k2(std::span<const u8, 16>(hk->data() + 16, 16));
    k1.DecryptXts(target->data.subspan(0, 0xC00), hdr, k2, 0, 0x200);
    if (std::memcmp(hdr.data() + 0x200, "NCA3", 4) != 0) {
        std::fprintf(stderr, "header decrypt failed\n");
        return 1;
    }

    const u8 keygen = hdr[0x206];
    const u8 kaek_idx = hdr[0x207];

    u32 sec0_media = 0;
    u32 sec0_media_end = 0;
    std::memcpy(&sec0_media, &hdr[0x240], 4);
    std::memcpy(&sec0_media_end, &hdr[0x244], 4);
    const u64 sec0_off = static_cast<u64>(sec0_media) * 0x200;
    const u64 sec0_size = static_cast<u64>(sec0_media_end - sec0_media) * 0x200;
    std::printf("keygen=%u kaek=%u sec0 media 0x%X..0x%X -> file 0x%llX size 0x%llX\n",
                keygen, kaek_idx, sec0_media, sec0_media_end,
                (unsigned long long)sec0_off, (unsigned long long)sec0_size);
    if (sec0_off + 4 > target->data.size()) {
        std::fprintf(stderr, "sec0 out of range\n");
        return 1;
    }

    struct Cand { std::array<u8, 16> k; std::string name; };
    std::vector<Cand> kaks;

    for (int mk = 0; mk < 32; ++mk) {
        char kn[64];
        std::snprintf(kn, sizeof(kn), "master_key_%02x", mk);
        auto m = ks.GetKey(kn);
        if (!m || m->size() < 16) {
            continue;
        }
        std::array<u8, 16> mk16{};
        std::memcpy(mk16.data(), m->data(), 16);
        kaks.push_back({mk16, std::string(kn) + " (raw)"});
        core::crypto::Aes128 c(mk16);
        for (u32 w = 0x100; w + 16 <= 0x200; w += 4) {
            std::array<u8, 16> out{};
            c.DecryptBlock(std::span<const u8, 16>(gh.data() + w, 16), out);
            char nm[96];
            std::snprintf(nm, sizeof(nm), "dec(%s, cart@0x%03X)", kn, w);
            kaks.push_back({out, nm});
        }
        for (u32 w = 0x100; w + 16 <= 0x200; w += 4) {
            std::array<u8, 16> out{};
            c.EncryptBlock(std::span<const u8, 16>(gh.data() + w, 16), out);
            char nm[96];
            std::snprintf(nm, sizeof(nm), "enc(%s, cart@0x%03X)", kn, w);
            kaks.push_back({out, nm});
        }
    }

    static const char* kDirectNames[] = {
        "aes_key_generation_source", "aes_kek_generation_source", "key_area_key_ocean",
        "key_area_key_system", "master_kek",
    };
    for (const char* nm : kDirectNames) {
        auto v = ks.GetKey(nm);
        if (v && v->size() >= 16) {
            std::array<u8, 16> k{};
            std::memcpy(k.data(), v->data(), 16);
            kaks.push_back({k, nm});
        }
    }
    for (int g = 0; g < 4; ++g) {
        for (int t = 0; t < 3; ++t) {
            auto v = ks.GetKeyAreaKey(static_cast<u8>(g), static_cast<u8>(t));
            if (v && v->size() >= 16) {
                std::array<u8, 16> k{};
                std::memcpy(k.data(), v->data(), 16);
                char nm[64];
                std::snprintf(nm, sizeof(nm), "kak_type%d_gen%d", t, g);
                kaks.push_back({k, nm});
            }
        }
    }
    std::printf("Key-Area-Key candidates: %zu\n", kaks.size());

    static const u32 kKeyAreaOffsets[] = {0x2C0, 0x300, 0x310, 0x320, 0x280};
    int hits = 0;
    long long tried = 0;

    for (const auto& cand : kaks) {
        core::crypto::Aes128 kak_cipher(cand.k);
        for (const u32 ka_off : kKeyAreaOffsets) {
            for (u32 idx = 0; idx < 4; ++idx) {
                if (ka_off + 16u * (idx + 1) > hdr.size()) {
                    continue;
                }
                std::array<u8, 16> sect_key{};
                std::array<u8, 16> enc{};
                std::memcpy(enc.data(), &hdr[ka_off + 16u * idx], 16);
                kak_cipher.DecryptBlock(enc, sect_key);

                for (int ctr_v = 0; ctr_v < 3; ++ctr_v) {
                    std::array<u8, 16> ctr{};
                    if (ctr_v == 0) {
                        std::memcpy(ctr.data(), &hdr[0x280 + 16u * idx], 16);
                    } else if (ctr_v == 1) {
                        for (int b = 0; b < 8; ++b) {
                            ctr[b] = static_cast<u8>((sec0_off >> ((7 - b) * 8)) & 0xFF);
                        }
                        ctr[8] = keygen;
                        ctr[9] = static_cast<u8>(idx);
                    } else {
                        for (int b = 0; b < 8; ++b) {
                            ctr[b] = static_cast<u8>((sec0_off >> ((7 - b) * 8)) & 0xFF);
                        }
                    }

                    std::vector<u8> probe(4, 0);
                    core::crypto::Aes128 sk(sect_key);
                    sk.DecryptCtr(target->data.subspan(sec0_off, 4), probe, ctr, 0);
                    ++tried;
                    if (probe[0] == 'P' && probe[1] == 'F' && probe[2] == 'S' &&
                        probe[3] == '0') {
                        std::printf("*** PFS0 MATCH ***\n  kak = %s\n  key_area = 0x%03X[%u]\n"
                                    "  ctr = variant %d\n  sect_key = ",
                                    cand.name.c_str(), ka_off, idx, ctr_v);
                        for (int i = 0; i < 16; ++i) {
                            std::printf("%02x", sect_key[i]);
                        }
                        std::printf("\n");
                        ++hits;
                    }
                }
            }
        }
    }
    std::printf("tried %lld combinations, %d hit(s)\n", tried, hits);
    return hits > 0 ? 0 : 1;
}
