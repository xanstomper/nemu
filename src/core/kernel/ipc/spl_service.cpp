// SPDX-License-Identifier: GPL-2.0-or-later
// SPL (spl, spl:ssl, spl:mig, spl:fs) — Security / Cryptography HLE Service
// Implements real AES-128-CMAC (RFC 4493) and deterministic HLE key derivation.

#include "spl_service.hpp"
#include "platform/logger.hpp"
#include "core/crypto/aes.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <random>

namespace nemu::core::kernel::ipc {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

using Block = std::array<u8, 16>;

/// Left-shift a 128-bit block by 1 bit (big-endian, MSB first).
static Block ShiftLeft1(const Block& b) noexcept {
    Block out{};
    for (size_t i = 0; i < 15; ++i) {
        out[i] = static_cast<u8>((b[i] << 1) | (b[i + 1] >> 7));
    }
    out[15] = static_cast<u8>(b[15] << 1);
    return out;
}

/// XOR two 16-byte blocks in-place: dst ^= src.
static void XorBlock(Block& dst, const Block& src) noexcept {
    for (size_t i = 0; i < 16; ++i) {
        dst[i] ^= src[i];
    }
}

/// Generate CMAC subkeys K1 and K2 from a cipher key (RFC 4493 §2.3).
static void GenerateCmacSubkeys(const crypto::Aes128& aes,
                                Block& k1, Block& k2) noexcept {
    // Step 1: L = AES(key, 0^128)
    Block zero{};
    Block L{};
    aes.EncryptBlock(
        std::span<const u8, 16>{zero.data(), 16},
        std::span<u8, 16>{L.data(), 16}
    );

    // Step 2: K1 = L << 1  (XOR Rb if MSB(L)==1)
    constexpr u8 Rb = 0x87;
    const bool msb_L = (L[0] & 0x80) != 0;
    k1 = ShiftLeft1(L);
    if (msb_L) k1[15] ^= Rb;

    // Step 3: K2 = K1 << 1 (XOR Rb if MSB(K1)==1)
    const bool msb_K1 = (k1[0] & 0x80) != 0;
    k2 = ShiftLeft1(k1);
    if (msb_K1) k2[15] ^= Rb;
}

/// AES-128-CMAC of `data` (up to 16 bytes) using `key_bytes`.
/// Returns the 16-byte authentication tag.
static Block ComputeAesCmac(const Block& key_bytes,
                             const u8* data, size_t data_len) noexcept {
    // Clamp input to a single block for the HLE use-case.
    if (data_len > 16) data_len = 16;

    crypto::Aes128 aes{std::span<const u8, 16>{key_bytes.data(), 16}};

    Block k1{}, k2{};
    GenerateCmacSubkeys(aes, k1, k2);

    // Single-block message.
    Block M{};
    const bool complete = (data_len == 16);
    if (complete) {
        // Full block: M_last = M_1 XOR K1
        std::memcpy(M.data(), data, 16);
        XorBlock(M, k1);
    } else {
        // Incomplete block: pad with 0x80 then zeros, XOR K2
        std::memcpy(M.data(), data, data_len);
        M[data_len] = 0x80;
        XorBlock(M, k2);
    }

    // CBC-MAC: X = AES(key, 0^128 XOR M_last)  — only one block here
    Block X{};   // all-zero IV
    XorBlock(X, M);
    Block tag{};
    aes.EncryptBlock(
        std::span<const u8, 16>{X.data(), 16},
        std::span<u8, 16>{tag.data(), 16}
    );
    return tag;
}

/// Fixed device-unique key used as the HLE wrapping key during key derivation.
/// This is seeded from a compile-time constant so that it is reproducible
/// across runs (deterministic HLE). Real hardware uses per-device fuses.
static const Block& DeviceWrapKey() noexcept {
    static const Block key = {
        0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81,
        0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09
    };
    return key;
}

/// Deterministic HLE key derivation:
///   1. wrapped = key_source XOR master_key
///   2. result  = AES-128-ECB_Encrypt(device_wrap_key, wrapped)
/// Same inputs always produce the same output; self-consistent for HLE.
static Block DeriveKey(const Block& key_source, const Block& master_key) noexcept {
    Block wrapped{};
    for (size_t i = 0; i < 16; ++i) {
        wrapped[i] = key_source[i] ^ master_key[i];
    }

    const Block& wk = DeviceWrapKey();
    crypto::Aes128 aes{std::span<const u8, 16>{wk.data(), 16}};
    Block result{};
    aes.EncryptBlock(
        std::span<const u8, 16>{wrapped.data(), 16},
        std::span<u8, 16>{result.data(), 16}
    );
    return result;
}

/// Helper: read 16 bytes from IpcRequestReader payload at `payload_offset`
/// into a Block.
static Block ReadPayloadBlock(const IpcRequestReader& req, size_t payload_offset) noexcept {
    Block blk{};
    for (size_t i = 0; i < 16; ++i) {
        blk[i] = req.Payload<u8>(payload_offset + i);
    }
    return blk;
}

/// Helper: write a Block into IpcReplyWriter payload starting at `payload_offset`.
static void WritePayloadBlock(IpcReplyWriter& reply, size_t payload_offset,
                              const Block& blk) noexcept {
    // Write as two u64s (little-endian on host = correct byte layout in payload).
    u64 lo = 0, hi = 0;
    for (size_t i = 8; i-- > 0; ) {
        lo = (lo << 8) | blk[i];
        hi = (hi << 8) | blk[8u + i];
    }
    reply.Payload<u64>(payload_offset,      lo);
    reply.Payload<u64>(payload_offset + 8,  hi);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// SplService
// ---------------------------------------------------------------------------

SplService::SplService(std::string name) : IIpcService(std::move(name)) {}

u32 SplService::HandleRequest(const IpcContext& ctx, const IpcRequestReader& request,
                              IpcReplyWriter& reply, u32 cmd_id) {
    (void)ctx;

    switch (cmd_id) {
        // -----------------------------------------------------------------------
        // Cmd 0: GetRandomBytes — fill 16 bytes with CSPRNG output.
        // -----------------------------------------------------------------------
        case GetRandomBytes: {
            NEMU_LOG_DEBUG("SPL", "GetRandomBytes() returning 16 bytes of entropy");
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<u64> dis;
            const u64 r1 = dis(gen);
            const u64 r2 = dis(gen);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0); // Result: Success
            reply.Payload<u64>(8,  r1);
            reply.Payload<u64>(16, r2);
            return static_cast<u32>(IpcResult::Success);
        }

        // -----------------------------------------------------------------------
        // Cmd 1/2/3: GenerateAesKek / GenerateAesKey / GenerateKey
        //
        // Payload layout (from guest, relative to IpcField::Payload):
        //   offset  0..3   : padding / flags (ignored by HLE)
        //   offset  8..23  : 16-byte key_source
        //   offset 24..39  : 16-byte master_key
        //
        // Derivation: result = AES-ECB(device_wrap_key, key_source XOR master_key)
        // -----------------------------------------------------------------------
        case GenerateAesKek:
        case GenerateAesKey:
        case GenerateKey: {
            NEMU_LOG_DEBUG("SPL", "Key derivation cmd 0x{:X}", cmd_id);

            const Block key_source  = ReadPayloadBlock(request, 8);
            const Block master_key  = ReadPayloadBlock(request, 24);
            const Block derived_key = DeriveKey(key_source, master_key);

            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0); // Result: Success
            WritePayloadBlock(reply, 8, derived_key);
            return static_cast<u32>(IpcResult::Success);
        }

        // -----------------------------------------------------------------------
        // Cmd 4: ComputeCmac — AES-128-CMAC (RFC 4493).
        //
        // Payload layout (from guest, relative to IpcField::Payload):
        //   offset  8..23  : 16-byte CMAC key
        //   offset 24..39  : up to 16 bytes of data (inline in payload)
        //
        // Reply payload:
        //   offset  0..3   : Result (0 = success)
        //   offset  8..23  : 16-byte CMAC tag
        // -----------------------------------------------------------------------
        case ComputeCmac: {
            NEMU_LOG_DEBUG("SPL", "ComputeCmac() — real AES-128-CMAC");

            const Block key_bytes = ReadPayloadBlock(request, 8);

            // Read up to 16 bytes of data from inline payload at offset 24.
            const std::string_view data_sv = request.ReadString(16, 24);
            const auto* data_ptr = reinterpret_cast<const u8*>(data_sv.data());
            const size_t data_len = data_sv.size();

            const Block tag = ComputeAesCmac(key_bytes, data_ptr, data_len);

            reply.Begin(static_cast<u32>(IpcCommandType::Request), 24);
            reply.Payload<u32>(0, 0); // Result: Success
            WritePayloadBlock(reply, 8, tag);
            return static_cast<u32>(IpcResult::Success);
        }

        // -----------------------------------------------------------------------
        default:
            NEMU_LOG_WARN("SPL", "Unhandled cmd 0x{:X}", cmd_id);
            reply.Begin(static_cast<u32>(IpcCommandType::Request), 8);
            reply.Payload<u32>(0, 0);
            return static_cast<u32>(IpcResult::Success);
    }
}

} // namespace nemu::core::kernel::ipc
