#pragma once

#include "core/types.hpp"
#include <span>
#include <array>
#include <vector>
#include <cstddef>

namespace nemu::core::crypto {

class Aes128 {
public:
    static constexpr size_t KEY_SIZE = 16;
    static constexpr size_t BLOCK_SIZE = 16;

    Aes128() = default;
    explicit Aes128(std::span<const u8, KEY_SIZE> key);

    void SetKey(std::span<const u8, KEY_SIZE> key);

    /// Encrypt a single 16-byte block
    void EncryptBlock(std::span<const u8, BLOCK_SIZE> src, std::span<u8, BLOCK_SIZE> dst) const;

    /// Decrypt a single 16-byte block
    void DecryptBlock(std::span<const u8, BLOCK_SIZE> src, std::span<u8, BLOCK_SIZE> dst) const;

    /// AES-128-CTR stream encryption/decryption (symmetric)
    /// Counter increments as a 64-bit big-endian integer in the upper/lower 8 bytes as specified by Switch format
    void DecryptCtr(
        std::span<const u8> src,
        std::span<u8> dst,
        std::span<const u8, BLOCK_SIZE> iv,
        u64 block_offset = 0
    ) const;

    /// AES-128-XTS sector decryption (used for NCA headers and storage partitions)
    /// @param key2 The second 128-bit key (tweak key)
    /// @param sector_index The starting sector (block) index for tweak computation
    /// @param sector_size Typically 0x200 (512 bytes)
    void DecryptXts(
        std::span<const u8> src,
        std::span<u8> dst,
        const Aes128& key2,
        u64 sector_index,
        size_t sector_size = 0x200
    ) const;

private:
    std::array<u32, 44> round_keys_enc_{};
    std::array<u32, 44> round_keys_dec_{};
    bool key_set_{false};
};

} // namespace nemu::core::crypto
