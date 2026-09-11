#include "nca.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::loader {

bool NcaReader::Initialize(std::span<const u8> data, const crypto::KeyStore* key_store) {
    if (data.size() < HEADER_SIZE) {
        return false;
    }

    raw_data_ = data;
    is_encrypted_ = false;

    // Check if magic at offset 0x200 matches unencrypted NCA
    u32 unenc_magic = 0;
    std::memcpy(&unenc_magic, data.data() + 0x200, sizeof(u32));

    if (unenc_magic == NCA3_MAGIC || unenc_magic == NCA2_MAGIC || unenc_magic == NCA0_MAGIC) {
        std::copy_n(data.data(), HEADER_SIZE, decrypted_header_.data());
        magic_ = unenc_magic;
    } else if (key_store != nullptr) {
        // Try to decrypt header using header_key (32 bytes: key1 [16], key2 [16])
        auto header_key_bytes = key_store->GetHeaderKey();
        if (header_key_bytes.has_value() && header_key_bytes->size() >= 32) {
            crypto::Aes128 key1(std::span<const u8, 16>(header_key_bytes->data(), 16));
            crypto::Aes128 key2(std::span<const u8, 16>(header_key_bytes->data() + 16, 16));

            key1.DecryptXts(
                data.subspan(0, HEADER_SIZE),
                decrypted_header_,
                key2,
                0,
                BLOCK_SIZE
            );

            std::memcpy(&magic_, decrypted_header_.data() + 0x200, sizeof(u32));
            if (magic_ == NCA3_MAGIC || magic_ == NCA2_MAGIC || magic_ == NCA0_MAGIC) {
                is_encrypted_ = true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    const u8* h = decrypted_header_.data() + 0x200;

    content_type_ = static_cast<NcaContentType>(h[5]);
    key_generation_ = h[6];
    kaek_index_ = h[7];
    std::memcpy(&content_size_, h + 8, sizeof(u64));
    std::memcpy(&title_id_, h + 16, sizeof(u64));

    // Parse section entries at 0x240 (4 entries of 16 bytes: relative to start of header: 0x240)
    for (u32 s = 0; s < 4; ++s) {
        const u8* sec_ptr = decrypted_header_.data() + 0x240 + s * 16;
        u32 start_block = 0;
        u32 end_block = 0;
        std::memcpy(&start_block, sec_ptr, sizeof(u32));
        std::memcpy(&end_block, sec_ptr + 4, sizeof(u32));

        if (end_block > start_block) {
            sections_[s].section_index = s;
            sections_[s].offset = static_cast<u64>(start_block) * BLOCK_SIZE;
            sections_[s].size = static_cast<u64>(end_block - start_block) * BLOCK_SIZE;
            sections_[s].encryption_type = is_encrypted_ ? NcaEncryptionType::Ctr : NcaEncryptionType::None;

            // Compute default CTR from section index and generation
            for (size_t b = 0; b < 8; ++b) {
                sections_[s].ctr[b] = static_cast<u8>((sections_[s].offset >> ((7 - b) * 8)) & 0xFF);
            }
            sections_[s].ctr[8] = static_cast<u8>(s);
        } else {
            sections_[s].size = 0;
        }
    }

    // Read Key Area at 0x300 (4 keys of 16 bytes)
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

    if (!is_encrypted_ || sec.encryption_type == NcaEncryptionType::None) {
        std::copy(src_slice.begin(), src_slice.end(), output.begin());
        return output;
    }

    // Encrypted section: unwrap key area key and decrypt using CTR mode
    if (key_store == nullptr) {
        return std::nullopt;
    }

    auto kak = key_store->GetKeyAreaKey(key_generation_, 0);
    if (!kak.has_value() || kak->size() < 16) {
        return std::nullopt;
    }

    crypto::Aes128 kak_cipher(std::span<const u8, 16>(kak->data(), 16));
    std::array<u8, 16> decrypted_title_key{};
    kak_cipher.DecryptBlock(key_area_[kaek_index_ & 3], decrypted_title_key);

    crypto::Aes128 section_cipher(decrypted_title_key);
    section_cipher.DecryptCtr(src_slice, output, sec.ctr, 0);

    return output;
}

} // namespace nemu::core::loader
