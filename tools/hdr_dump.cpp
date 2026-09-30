// Diagnostic: dump the FULL decrypted NCA header (0xC00) using NEMU's own AES,
// so the real field offsets can be read instead of guessed from the spec.
//
//   g++ -std=c++20 -I src tools/hdr_dump.cpp -Lbuild/lib -lNemu.Loader \
//       -lNemu.Crypto -lNemu.Platform -lNemu.FileSystem -lNemu.Memory \
//       -lNemu.Cpu -lzstd -lpthread -o /tmp/hdr_dump

#include "core/loader/xci.hpp"
#include "core/loader/nca.hpp"
#include "core/crypto/aes.hpp"
#include "core/crypto/key_store.hpp"
#include "core/types.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace nemu;

static void Hex(const std::vector<u8>& d, size_t off, size_t len) {
    for (size_t i = 0; i < len; i += 16) {
        std::printf("  %04zX ", off + i);
        for (size_t j = 0; j < 16 && off + i + j < len + off && off + i + j < d.size(); ++j) {
            std::printf("%02x ", d[off + i + j]);
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <cart.xci> [keys_dir]\n", argv[0]);
        return 2;
    }
    core::crypto::KeyStore ks;
    std::string keys_dir = (argc > 2) ? argv[2] : "games";
    ks.LoadFromFile(keys_dir + "/prod.keys");
    ks.LoadFromFile(keys_dir + "/title.keys");

    auto hk = ks.GetHeaderKey();
    if (!hk || hk->size() < 32) {
        std::printf("no header_key\n");
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    file.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<u8> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

    core::loader::XciArchive cart;
    if (!cart.Initialize(data)) {
        return 1;
    }
    std::vector<core::loader::XciPayload> payloads;
    if (!cart.UnpackGame(payloads)) {
        return 1;
    }

    for (const auto& p : payloads) {
        if (p.name.find(".cnmt.") != std::string::npos) {
            continue;
        }
        if (p.data.size() < 0x2000 || p.name.find(".nca") == std::string::npos) {
            continue;
        }
        // Pick a Program NCA (largest non-meta).
        core::loader::NcaReader probe;
        if (!probe.Initialize(p.data, &ks) ||
            probe.GetContentType() != core::loader::NcaContentType::Program) {
            continue;
        }
        std::printf("=== %s (%zu B) ===\n", p.name.c_str(), p.data.size());

        // Decrypt the full 0xC00 with NEMU's own primitives.
        std::vector<u8> hdr(0xC00, 0);
        core::crypto::Aes128 k1(std::span<const u8, 16>(hk->data(), 16));
        core::crypto::Aes128 k2(std::span<const u8, 16>(hk->data() + 16, 16));
        k1.DecryptXts(p.data.subspan(0, 0xC00), hdr, k2, 0, 0x200);

        std::printf("magic@0x200 = %02x %02x %02x %02x\n", hdr[0x200], hdr[0x201], hdr[0x202],
                    hdr[0x203]);
        std::printf("raw 0x200..0x280:\n");
        Hex(hdr, 0x200, 0x80);
        std::printf("distribution=%u content_type=%u keygen=%u kaek_idx=%u crypto_type=0x%02x\n",
                    hdr[0x204], hdr[0x205], hdr[0x206], hdr[0x207], hdr[0x209]);
        std::printf("title_id = "); Hex(hdr, 0x210, 8); std::printf("\n");
        std::printf("content_size = 0x%llx\n",
                    (unsigned long long)__builtin_bswap64(*(uint64_t*)&hdr[0x230]));
        std::printf("rights_id = "); Hex(hdr, 0x220, 16); std::printf("\n");

        std::printf("\n-- section_ctrs @0x280 --\n");
        Hex(hdr, 0x280, 0x40);
        std::printf("\n-- key_area @0x2C0 --\n");
        Hex(hdr, 0x2C0, 0x40);
        std::printf("\n-- aes_key_generation @0x300 --\n");
        Hex(hdr, 0x300, 0x20);
        std::printf("\n-- 0x400 region header --\n");
        Hex(hdr, 0x400, 0x10);
        std::printf("\n-- 0x420..0x500 --\n");
        Hex(hdr, 0x420, 0xE0);
        std::printf("\n-- segment_region_entries @0x500 --\n");
        Hex(hdr, 0x500, 0x40);
        std::printf("\n-- 0xC00 tail / 0x700-0x780 --\n");
        Hex(hdr, 0x700, 0x80);

        // Decisive test: section 0 lives at media block 0x3CEA0 (per the 0x240
        // section table). Derive its key from the key_area and AES-CTR decrypt.
        // Correct handling must yield "PFS0" (50 46 53 30) at offset 0.
        std::printf("\n=== ExeFS decrypt test (sec0 @ media block 0x3CEA0) ===\n");
        auto kak = ks.GetKeyAreaKey(hdr[0x206], 0);
        if (!kak) {
            std::printf("  no key_area_key_application_%02x\n", hdr[0x206]);
            return 0;
        }

        const u64 sec0_off = 0x3CEA0ull * 0x200;

        // Exhaustive sweep: every plausible key-area offset in the header, every
        // key index, every candidate Key-Area-Key derivation, and both CTR
        // layouts. The ExeFS must decrypt to "PFS0"; that is a 1-in-2^32 oracle.
        auto aekgen = ks.GetKey("aes_key_generation_source");
        auto aeskek = ks.GetKey("aes_kek_generation_source");
        auto kak_app = ks.GetKeyAreaKey(hdr[0x206], 0);

        std::vector<std::array<u8, 16>> kaks;
        std::vector<std::string> kak_names;
        if (kak_app) {
            std::array<u8, 16> k{};
            std::memcpy(k.data(), kak_app->data(), 16);
            kaks.push_back(k);
            kak_names.push_back("kak_application");
        }
        if (aekgen) {
            core::crypto::Aes128 g(std::span<const u8, 16>(aekgen->data(), 16));
            std::array<u8, 16> k{};
            g.DecryptBlock(std::span<const u8, 16>(hdr.data() + 0x300, 16), k);
            kaks.push_back(k);
            kak_names.push_back("dec(aes_key_gener, hdr@0x300)");
        }
        if (aeskek) {
            core::crypto::Aes128 g(std::span<const u8, 16>(aeskek->data(), 16));
            std::array<u8, 16> k{};
            g.DecryptBlock(std::span<const u8, 16>(hdr.data() + 0x300, 16), k);
            kaks.push_back(k);
            kak_names.push_back("dec(aes_kek_gen, hdr@0x300)");
        }
        // Also try the raw header bytes as the KAK (some dumps store it plainly).

        // Also try every keyblob master key directly: gamecard NCAs derive the
        // Key-Area-Key from master_key[key_generation] with no further unwrap.
        for (int mk = 0; mk < 32; ++mk) {
            char name[64];
            std::snprintf(name, sizeof(name), "master_key_%02x", mk);
            auto m = ks.GetKey(name);
            if (!m || m->size() < 16) {
                continue;
            }
            std::array<u8, 16> k{};
            std::memcpy(k.data(), m->data(), 16);
            kaks.push_back(k);
            kak_names.push_back(name);
        }

        int hits = 0;
        for (size_t ki = 0; ki < kaks.size(); ++ki) {
            core::crypto::Aes128 kak_cipher(kaks[ki]);

            for (u32 ka_off = 0x200; ka_off + 64 <= 0xC00; ka_off += 0x10) {
                for (u32 ka_idx = 0; ka_idx < 4; ++ka_idx) {
                    std::array<u8, 16> sect_key{};
                    std::array<u8, 16> enc{};
                    std::memcpy(enc.data(), &hdr[ka_off + 16u * ka_idx], 16);
                    kak_cipher.DecryptBlock(enc, sect_key);

                    for (int ctr_style = 0; ctr_style < 3; ++ctr_style) {
                        // Sweep the media offset as well: the 0x240 table gives
                        // media offsets, but NEMU may be applying them with the
                        // wrong unit or base. Probe every plausible candidate.
                        static const u64 kBlkCandidates[] = {
                            sec0_off / 0x200, sec0_off, 0x3CEA0ull, 0x4F980ull,
                            0x79D4000ull / 0x200, 0x20ull, 0xE0ull, 0x6ull,
                        };
                        for (u64 cand : kBlkCandidates) {
                            std::array<u8, 16> ctr{};
                            for (int b = 0; b < 8; ++b) {
                                ctr[b] = static_cast<u8>((cand >> ((7 - b) * 8)) & 0xFF);
                            }
                            if (ctr_style == 0) {
                                ctr[8] = hdr[0x206];
                                ctr[9] = 0;
                            } else if (ctr_style == 1) {
                                ctr[8] = 0;
                            }

                            const u64 off = cand * 0x200;
                            if (off + 4 > p.data.size()) {
                                continue;
                            }
                            std::vector<u8> probe_out(4, 0);
                            core::crypto::Aes128 sk(sect_key);
                            sk.DecryptCtr(p.data.subspan(off, 4), probe_out, ctr, 0);
                            if (probe_out[0] == 'P' && probe_out[1] == 'F' &&
                                probe_out[2] == 'S' && probe_out[3] == '0') {
                                std::printf(
                                    "  *** PFS0 MATCH: kak=%s key_area@0x%03X[%u] ctr=%d off=0x%llx\n",
                                    kak_names[ki].c_str(), ka_off, ka_idx, ctr_style,
                                    (unsigned long long)off);
                                ++hits;
                            }
                        }
                    }
                }
            }
        }
        std::printf("  sweep done, %d hit(s) out of %zu\n", hits, kaks.size() * 160 * 4 * 2);
        return 0;
    }
    std::printf("no program NCA found\n");
    return 1;
}
