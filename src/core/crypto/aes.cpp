#include "aes.hpp"
#include <cstring>
#include <algorithm>

namespace nemu::core::crypto {

namespace {

// Standard AES S-Box
constexpr std::array<u8, 256> SBOX = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

// Standard AES Inverse S-Box
constexpr std::array<u8, 256> INV_SBOX = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

constexpr std::array<u8, 10> RCON = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

inline u32 SubWord(u32 w) noexcept {
    return (static_cast<u32>(SBOX[(w >> 24) & 0xFF]) << 24) |
           (static_cast<u32>(SBOX[(w >> 16) & 0xFF]) << 16) |
           (static_cast<u32>(SBOX[(w >> 8) & 0xFF]) << 8) |
           (static_cast<u32>(SBOX[w & 0xFF]));
}

inline u32 RotWord(u32 w) noexcept {
    return (w << 8) | (w >> 24);
}

inline u8 Xtime(u8 b) noexcept {
    return static_cast<u8>((b << 1) ^ (((b >> 7) & 1) * 0x1b));
}

inline u8 MultiplyGf(u8 a, u8 b) noexcept {
    u8 p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        bool hi = (a & 0x80) != 0;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

} // namespace

Aes128::Aes128(std::span<const u8, KEY_SIZE> key) {
    SetKey(key);
}

void Aes128::SetKey(std::span<const u8, KEY_SIZE> key) {
    // Key expansion for AES-128 (4 words input -> 44 words round keys)
    for (size_t i = 0; i < 4; ++i) {
        round_keys_enc_[i] = (static_cast<u32>(key[4 * i]) << 24) |
                             (static_cast<u32>(key[4 * i + 1]) << 16) |
                             (static_cast<u32>(key[4 * i + 2]) << 8) |
                             (static_cast<u32>(key[4 * i + 3]));
    }

    for (size_t i = 4; i < 44; ++i) {
        u32 temp = round_keys_enc_[i - 1];
        if ((i % 4) == 0) {
            temp = SubWord(RotWord(temp)) ^ (static_cast<u32>(RCON[(i / 4) - 1]) << 24);
        }
        round_keys_enc_[i] = round_keys_enc_[i - 4] ^ temp;
    }

    // Inverse round keys for decryption
    for (size_t r = 0; r <= 10; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            round_keys_dec_[r * 4 + c] = round_keys_enc_[(10 - r) * 4 + c];
        }
    }

    key_set_ = true;
}

void Aes128::EncryptBlock(std::span<const u8, BLOCK_SIZE> src, std::span<u8, BLOCK_SIZE> dst) const {
    std::array<u8, 16> state{};
    std::copy_n(src.data(), 16, state.data());

    // Initial AddRoundKey
    for (size_t c = 0; c < 4; ++c) {
        u32 k = round_keys_enc_[c];
        state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
        state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
        state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
        state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
    }

    // Rounds 1 to 9
    for (size_t round = 1; round <= 9; ++round) {
        // SubBytes
        for (size_t i = 0; i < 16; ++i) state[i] = SBOX[state[i]];

        // ShiftRows
        u8 t1 = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t1;
        u8 t2 = state[2]; u8 t6 = state[6]; state[2] = state[10]; state[6] = state[14]; state[10] = t2; state[14] = t6;
        u8 t15 = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t15;

        // MixColumns
        for (size_t c = 0; c < 4; ++c) {
            u8 a0 = state[c * 4 + 0];
            u8 a1 = state[c * 4 + 1];
            u8 a2 = state[c * 4 + 2];
            u8 a3 = state[c * 4 + 3];
            state[c * 4 + 0] = Xtime(a0) ^ Xtime(a1) ^ a1 ^ a2 ^ a3;
            state[c * 4 + 1] = a0 ^ Xtime(a1) ^ Xtime(a2) ^ a2 ^ a3;
            state[c * 4 + 2] = a0 ^ a1 ^ Xtime(a2) ^ Xtime(a3) ^ a3;
            state[c * 4 + 3] = Xtime(a0) ^ a0 ^ a1 ^ a2 ^ Xtime(a3);
        }

        // AddRoundKey
        for (size_t c = 0; c < 4; ++c) {
            u32 k = round_keys_enc_[round * 4 + c];
            state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
            state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
            state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
            state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
        }
    }

    // Round 10 (Final round, no MixColumns)
    for (size_t i = 0; i < 16; ++i) state[i] = SBOX[state[i]];

    u8 t1 = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t1;
    u8 t2 = state[2]; u8 t6 = state[6]; state[2] = state[10]; state[6] = state[14]; state[10] = t2; state[14] = t6;
    u8 t15 = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t15;

    for (size_t c = 0; c < 4; ++c) {
        u32 k = round_keys_enc_[10 * 4 + c];
        state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
        state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
        state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
        state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
    }

    std::copy_n(state.data(), 16, dst.data());
}

void Aes128::DecryptBlock(std::span<const u8, BLOCK_SIZE> src, std::span<u8, BLOCK_SIZE> dst) const {
    std::array<u8, 16> state{};
    std::copy_n(src.data(), 16, state.data());

    // Round 0 (Key from round 10)
    for (size_t c = 0; c < 4; ++c) {
        u32 k = round_keys_dec_[c];
        state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
        state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
        state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
        state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
    }

    for (size_t round = 1; round <= 9; ++round) {
        // InvShiftRows
        u8 t13 = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = t13;
        u8 t2 = state[2]; u8 t6 = state[6]; state[2] = state[10]; state[6] = state[14]; state[10] = t2; state[14] = t6;
        u8 t3 = state[3]; state[3] = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = t3;

        // InvSubBytes
        for (size_t i = 0; i < 16; ++i) state[i] = INV_SBOX[state[i]];

        // AddRoundKey
        for (size_t c = 0; c < 4; ++c) {
            u32 k = round_keys_dec_[round * 4 + c];
            state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
            state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
            state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
            state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
        }

        // InvMixColumns
        for (size_t c = 0; c < 4; ++c) {
            u8 a0 = state[c * 4 + 0];
            u8 a1 = state[c * 4 + 1];
            u8 a2 = state[c * 4 + 2];
            u8 a3 = state[c * 4 + 3];
            state[c * 4 + 0] = MultiplyGf(a0, 0x0e) ^ MultiplyGf(a1, 0x0b) ^ MultiplyGf(a2, 0x0d) ^ MultiplyGf(a3, 0x09);
            state[c * 4 + 1] = MultiplyGf(a0, 0x09) ^ MultiplyGf(a1, 0x0e) ^ MultiplyGf(a2, 0x0b) ^ MultiplyGf(a3, 0x0d);
            state[c * 4 + 2] = MultiplyGf(a0, 0x0d) ^ MultiplyGf(a1, 0x09) ^ MultiplyGf(a2, 0x0e) ^ MultiplyGf(a3, 0x0b);
            state[c * 4 + 3] = MultiplyGf(a0, 0x0b) ^ MultiplyGf(a1, 0x0d) ^ MultiplyGf(a2, 0x09) ^ MultiplyGf(a3, 0x0e);
        }
    }

    // Final Round
    u8 t13 = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = t13;
    u8 t2 = state[2]; u8 t6 = state[6]; state[2] = state[10]; state[6] = state[14]; state[10] = t2; state[14] = t6;
    u8 t3 = state[3]; state[3] = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = t3;

    for (size_t i = 0; i < 16; ++i) state[i] = INV_SBOX[state[i]];

    for (size_t c = 0; c < 4; ++c) {
        u32 k = round_keys_dec_[10 * 4 + c];
        state[c * 4 + 0] ^= static_cast<u8>((k >> 24) & 0xFF);
        state[c * 4 + 1] ^= static_cast<u8>((k >> 16) & 0xFF);
        state[c * 4 + 2] ^= static_cast<u8>((k >> 8) & 0xFF);
        state[c * 4 + 3] ^= static_cast<u8>(k & 0xFF);
    }

    std::copy_n(state.data(), 16, dst.data());
}

void Aes128::DecryptCtr(
    std::span<const u8> src,
    std::span<u8> dst,
    std::span<const u8, BLOCK_SIZE> iv,
    u64 block_offset
) const {
    const size_t total_bytes = std::min(src.size(), dst.size());
    std::array<u8, 16> counter{};
    std::copy_n(iv.data(), 16, counter.data());

    // Add block_offset to the 64-bit big-endian counter at the lower 8 bytes (or upper 8 bytes depending on convention)
    // Standard Nintendo CTR counter addition:
    u64 current_counter_val = 0;
    for (size_t i = 0; i < 8; ++i) {
        current_counter_val = (current_counter_val << 8) | counter[8 + i];
    }
    current_counter_val += block_offset;

    std::array<u8, 16> keystream{};
    size_t offset = 0;

    while (offset < total_bytes) {
        // Write updated counter
        for (size_t i = 0; i < 8; ++i) {
            counter[15 - i] = static_cast<u8>(current_counter_val & 0xFF);
            current_counter_val >>= 8;
        }

        EncryptBlock(counter, keystream);

        const size_t chunk = std::min(size_t{16}, total_bytes - offset);
        for (size_t i = 0; i < chunk; ++i) {
            dst[offset + i] = src[offset + i] ^ keystream[i];
        }

        offset += chunk;
        current_counter_val += 1;
    }
}

void Aes128::DecryptXts(
    std::span<const u8> src,
    std::span<u8> dst,
    const Aes128& key2,
    u64 sector_index,
    size_t sector_size
) const {
    const size_t total_bytes = std::min(src.size(), dst.size());
    const size_t sectors = total_bytes / sector_size;

    for (size_t s = 0; s < sectors; ++s) {
        const u64 curr_sector = sector_index + s;
        const size_t sector_byte_offset = s * sector_size;

        // Compute tweak T0 = Encrypt_key2(sector_index)
        std::array<u8, 16> tweak{};
        std::array<u8, 16> sector_bytes{};
        for (size_t i = 0; i < 8; ++i) {
            sector_bytes[i] = static_cast<u8>((curr_sector >> (8 * i)) & 0xFF);
        }

        key2.EncryptBlock(sector_bytes, tweak);

        const size_t blocks_in_sector = sector_size / 16;
        for (size_t b = 0; b < blocks_in_sector; ++b) {
            const size_t block_offset = sector_byte_offset + b * 16;
            if (block_offset + 16 > total_bytes) break;

            std::array<u8, 16> block_in{};
            std::array<u8, 16> block_out{};

            for (size_t i = 0; i < 16; ++i) {
                block_in[i] = src[block_offset + i] ^ tweak[i];
            }

            DecryptBlock(block_in, block_out);

            for (size_t i = 0; i < 16; ++i) {
                dst[block_offset + i] = block_out[i] ^ tweak[i];
            }

            // Multiply tweak by alpha in GF(2^128)
            u8 carry = 0;
            for (size_t i = 0; i < 16; ++i) {
                u8 next_carry = (tweak[i] >> 7) & 1;
                tweak[i] = static_cast<u8>((tweak[i] << 1) | carry);
                carry = next_carry;
            }
            if (carry) {
                tweak[0] ^= 0x87;
            }
        }
    }
}

} // namespace nemu::core::crypto
