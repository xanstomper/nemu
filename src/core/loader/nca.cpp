#include "nca.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::loader {

// ---------------------------------------------------------------------------
// Layout constants (hactool-compatible, verified against games/Terraria.xci).
// Main header @0x200 after two RSA signatures:
//   0x206 crypto_type (keygen field 1), 0x220 crypto_type2 (keygen field 2),
//   0x208 content_size, 0x210 title_id, 0x230 rights_id,
//   0x240 section table (4 x u32 media start/end),
//   0x300 key area (4 x 16 bytes encrypted)
// FS headers @0x400 + i*0x200 (XTS-encrypted as part of the header block):
//   +0x02 partition_type, +0x03 fs_type, +0x04 crypt_type,
//   +0x108 (PFS0 superblock) pfs0_offset, +0x118 pfs0_size,
//   IVFC levels at +0x18 (6 x 0x18), level-6 = data level,
//   +0x140 section_ctr (u64 LE -> byteswap into IV high half)
// ---------------------------------------------------------------------------

bool NcaReader::Initialize(std::span<const u8> data, const crypto::KeyStore* key_store) {
    if (data.size() < HEADER_SIZE) {
        return false;
    }

    raw_data_ = data;
    is_encrypted_ = false;

    u32 unenc_magic = 0;
    std::memcpy(&unenc_magic, data.data() + 0x200, sizeof(u32));

    const size_t header_avail = std::min(data.size(), FULL_HEADER_SIZE);

    if (unenc_magic == NCA3_MAGIC || unenc_magic == NCA2_MAGIC || unenc_magic == NCA0_MAGIC) {
        std::copy_n(data.data(), header_avail, decrypted_header_.data());
        magic_ = unenc_magic;
    } else if (key_store != nullptr) {
        auto header_key_bytes = key_store->GetHeaderKey();
        if (!header_key_bytes.has_value() || header_key_bytes->size() < 32) {
            return false;
        }

        crypto::Aes128 key1(std::span<const u8, 16>(header_key_bytes->data(), 16));
        crypto::Aes128 key2(std::span<const u8, 16>(header_key_bytes->data() + 16, 16));

        // Decrypt the whole 0xC00 header region as consecutive XTS data units
        // (tweak index = unit / 0x200, matching hactool's sector numbering).
        key1.DecryptXts(
            data.subspan(0, header_avail),
            std::span<u8>(decrypted_header_.data(), header_avail),
            key2,
            0,
            BLOCK_SIZE
        );

        std::memcpy(&magic_, decrypted_header_.data() + 0x200, sizeof(u32));
        NEMU_LOG_INFO("Crypto", "NCA header decrypt: raw_magic=0x{:08X} after_XTS=0x{:08X}",
                      unenc_magic, magic_);
        if (magic_ == NCA3_MAGIC || magic_ == NCA2_MAGIC || magic_ == NCA0_MAGIC) {
            is_encrypted_ = true;
        } else {
            return false;
        }
    } else {
        return false;
    }

    const u8* h = decrypted_header_.data() + 0x200;

    content_type_ = static_cast<NcaContentType>(h[5]);
    crypto_type_ = h[6];
    kaek_index_ = h[7];
    std::memcpy(&content_size_, h + 8, sizeof(u64));
    std::memcpy(&title_id_, h + 16, sizeof(u64));
    crypto_type2_ = decrypted_header_[0x220];

    // Effective key generation: max(crypto_type, crypto_type2), then -1 when
    // nonzero (keygen 0 and 1 both mean master key 0). Verified: crypto_type=2,
    // crypto_type2=8 -> KAK application_07 decrypts the key area correctly.
    u8 gen = std::max(crypto_type_, crypto_type2_);
    effective_key_generation_ = (gen > 0) ? static_cast<u8>(gen - 1) : 0;

    // Rights ID at 0x230
    std::memcpy(rights_id_.data(), h + 0x30, 16);
    has_rights_id_ = std::any_of(rights_id_.begin(), rights_id_.end(), [](u8 b) { return b != 0; });

    // Section table at 0x240: 4 entries x (u32 media_start, u32 media_end)
    for (u32 s = 0; s < 4; ++s) {
        const u8* sec_ptr = decrypted_header_.data() + 0x240 + s * 16;
        u32 start_block = 0;
        u32 end_block = 0;
        std::memcpy(&start_block, sec_ptr, sizeof(u32));
        std::memcpy(&end_block, sec_ptr + 4, sizeof(u32));

        if (end_block > start_block && start_block != 0) {
            auto& sec = sections_[s];
            sec.section_index = s;
            sec.offset = static_cast<u64>(start_block) * BLOCK_SIZE;
            sec.size = static_cast<u64>(end_block - start_block) * BLOCK_SIZE;

            const u8* fh = decrypted_header_.data() + 0x400 + s * FS_HEADER_SIZE;
            sec.partition_type = fh[2];
            sec.fs_type = static_cast<NcaSectionFsType>(fh[3]);
            const u8 crypt = fh[4];
            if (crypt >= 1 && crypt <= 4) {
                sec.encryption_type = static_cast<NcaEncryptionType>(crypt);
            } else if (is_encrypted_ || has_rights_id_) {
                // Legacy fallback for synthetic/headerless NCAs: whole content
                // encrypted with CTR under the title/key-area key.
                sec.encryption_type = NcaEncryptionType::Ctr;
            } else {
                sec.encryption_type = NcaEncryptionType::None;
            }

            // IV high half = byteswapped section_ctr u64 (stored LE at +0x140).
            for (size_t b = 0; b < 8; ++b) {
                sec.ctr[b] = fh[0x140 + (7 - b)];
            }
            // IV low half = BE64(section_offset >> 4).
            const u64 lo = sec.offset >> 4;
            for (size_t b = 0; b < 8; ++b) {
                sec.ctr[8 + b] = static_cast<u8>((lo >> ((7 - b) * 8)) & 0xFF);
            }

            // Payload window from the FS superblock.
            sec.data_window_offset = 0;
            sec.data_window_size = sec.size;
            if (sec.fs_type == NcaSectionFsType::Pfs0) {
                // pfs0_superblock at fh+0x08 (hactool pfs0.h):
                //   +0x00 master_hash[0x20], +0x28 block_size, +0x2C always_2,
                //   +0x30 hash_table_offset, +0x38 hash_table_size,
                //   +0x40 pfs0_offset (superblock-relative 0x38),
                //   +0x48 pfs0_size   (superblock-relative 0x40)
                // Verified on Terraria: pfs0_offset=0x8000, size=0x2551d5c.
                u64 pfs0_offset = 0;
                std::memcpy(&pfs0_offset, fh + 0x08 + 0x38, sizeof(u64));
                u64 pfs0_size = 0;
                std::memcpy(&pfs0_size, fh + 0x08 + 0x40, sizeof(u64));
                if (pfs0_offset < sec.size) {
                    sec.data_window_offset = pfs0_offset;
                    sec.data_window_size = std::min(pfs0_size, sec.size - pfs0_offset);
                }
            } else if (sec.fs_type == NcaSectionFsType::Romfs) {
                // romfs_superblock ivfc header at fh+0x08 (magic,id,mh,num),
                // then 6 level headers of 0x18 at fh+0x18:
                //   u64 logical_offset, u64 hash_data_size, u32 block_log2, u32 pad
                // Level 6 (index 5) offset is where the RomFS data begins.
                u64 lvl6 = 0;
                std::memcpy(&lvl6, fh + 0x18 + 5 * 0x18, sizeof(u64));
                if (lvl6 < sec.size) {
                    sec.data_window_offset = lvl6;
                    sec.data_window_size = sec.size - lvl6;
                }
            }
        } else {
            sections_[s].size = 0;
        }
    }

    // Key area at 0x300 (still encrypted at this point; resolved on demand).
    for (u32 k = 0; k < 4; ++k) {
        std::memcpy(key_area_[k].data(), decrypted_header_.data() + 0x300 + k * 16, 16);
    }

    return true;
}

bool NcaReader::HasSection(u32 section_index) const {
    if (section_index >= 4) return false;
    return sections_[section_index].size > 0;
}

std::optional<NcaSectionInfo> NcaReader::GetSectionInfo(u32 section_index) const {
    if (!HasSection(section_index)) {
        return std::nullopt;
    }
    return sections_[section_index];
}

std::string NcaReader::GetRightsIdHex() const {
    if (!has_rights_id_) return {};
    return crypto::KeyStore::BytesToHex(rights_id_);
}

std::optional<std::array<u8, 16>> NcaReader::ResolveSectionKey(const crypto::KeyStore& key_store) const {
    // 1. Rights ID / title key path.
    if (has_rights_id_) {
        std::string rid_hex = GetRightsIdHex();
        auto title_key = key_store.GetTitleKey(rid_hex);
        if (!title_key.has_value() && rid_hex.size() >= 16) {
            title_key = key_store.GetTitleKey(rid_hex.substr(0, 16));
        }
        if (title_key.has_value() && title_key->size() >= 16) {
            std::array<u8, 16> key{};
            std::copy_n(title_key->data(), 16, key.data());
            return key;
        }
        return std::nullopt; // rights id present but no title key -> cannot decrypt
    }

    // 2. Key area path. KAEK index selects application/ocean/system; slot 2
    //    carries the CTR section key (hactool: decrypted_keys[2]).
    u8 type = kaek_index_ & 3;
    auto kak = key_store.GetKeyAreaKey(effective_key_generation_, type);
    if (!kak.has_value() || kak->size() < 16) {
        return std::nullopt;
    }
    crypto::Aes128 kak_cipher(std::span<const u8, 16>(kak->data(), 16));
    std::array<u8, 16> key{};
    kak_cipher.DecryptBlock(key_area_[2], key);
    return key;
}

std::optional<std::vector<u8>> NcaReader::ExtractSection(
    u32 section_index,
    const crypto::KeyStore* key_store
) const {
    if (!HasSection(section_index)) {
        return std::nullopt;
    }

    const auto& sec = sections_[section_index];
    if (sec.offset + sec.size > raw_data_.size()) {
        return std::nullopt;
    }

    std::vector<u8> output(static_cast<size_t>(sec.size));
    std::span<const u8> src_slice(raw_data_.data() + sec.offset, static_cast<size_t>(sec.size));

    if (sec.encryption_type == NcaEncryptionType::None) {
        std::copy(src_slice.begin(), src_slice.end(), output.begin());
        return output;
    }

    if (key_store == nullptr) {
        return std::nullopt;
    }

    auto section_key = ResolveSectionKey(*key_store);
    if (!section_key.has_value()) {
        return std::nullopt;
    }

    crypto::Aes128 section_cipher(*section_key);
    section_cipher.DecryptCtr(src_slice, output, sec.ctr, 0);
    return output;
}

std::optional<std::vector<u8>> NcaReader::ExtractSectionPayload(
    u32 section_index,
    const crypto::KeyStore* key_store
) const {
    auto whole = ExtractSection(section_index, key_store);
    if (!whole.has_value()) {
        return std::nullopt;
    }
    const auto& sec = sections_[section_index];
    if (sec.data_window_offset >= whole->size()) {
        return std::nullopt;
    }
    std::vector<u8> payload(
        whole->begin() + static_cast<long>(sec.data_window_offset),
        whole->begin() + static_cast<long>(std::min<u64>(sec.data_window_offset + sec.data_window_size,
                                                          whole->size())));
    return payload;
}

} // namespace nemu::core::loader
