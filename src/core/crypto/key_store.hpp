#pragma once

#include "core/types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace nemu::core::crypto {

class KeyStore {
public:
    KeyStore();
    ~KeyStore() = default;

    KeyStore(const KeyStore&) = delete;
    KeyStore& operator=(const KeyStore&) = delete;

    /// Load standard keys from a file (e.g. prod.keys or title.keys)
    bool LoadFromFile(std::string_view file_path);

    /// Load keys from memory text buffer
    bool LoadFromText(std::string_view text);

    /// Manually register a named cryptographic key
    void SetKey(std::string_view key_name, std::span<const u8> key_bytes);

    /// Retrieve a named cryptographic key
    [[nodiscard]] std::optional<std::vector<u8>> GetKey(std::string_view key_name) const;

    /// Check if a named key is present
    [[nodiscard]] bool HasKey(std::string_view key_name) const;

    /// Total number of loaded keys
    [[nodiscard]] size_t Count() const;

    /// Helper to get the 32-byte header key (key1: 16 bytes, key2: 16 bytes)
    [[nodiscard]] std::optional<std::vector<u8>> GetHeaderKey() const;

    /// Helper to get a key area key by generation index (0..31) and type (0=Application, 1=Ocean, 2=System)
    [[nodiscard]] std::optional<std::vector<u8>> GetKeyAreaKey(u8 generation, u8 type = 0) const;

    /// Retrieve title key associated with a 32-character hex rights ID
    [[nodiscard]] std::optional<std::vector<u8>> GetTitleKey(std::string_view rights_id_hex) const;

    /// Attempt to resolve the title key for a Rights ID, first from an already
    /// derived key, then by decrypting an encrypted title-key blob with the
    /// matching titlekek (titlekek_index or titlekek_XX names).
    [[nodiscard]] std::optional<std::vector<u8>> GetTitleKeyDecrypted(
        std::string_view rights_id_hex, std::span<const u8> encrypted_title_key = {}) const;

    /// Auto-detect and load keys from standard paths (save:/keys/, sdmc:/switch/, etc.)
    bool LoadDefaultKeys();

    /// Summary of which critical prod.keys/title.keys entries are present.
    struct KeyCompleteness {
        bool has_header_key{false};
        bool has_master_key{false};
        bool has_key_area_key{false};
        bool has_titlekek{false};
        size_t title_key_count{0};
    };

    /// Probe which boot-critical key families are available (for diagnostics).
    /// Never logs keys themselves, only presence.
    [[nodiscard]] KeyCompleteness GetKeyCompleteness() const;

    /// Convert a 32-hex character string to a 16-byte array
    static std::optional<std::vector<u8>> HexToBytes(std::string_view hex);

    /// Convert a byte span to a lowercase hex string
    static std::string BytesToHex(std::span<const u8> bytes);

private:
    /// Resolve the titlekek for a rights ID (titlekek_XX / titlekek_index / titlekek).
    [[nodiscard]] std::optional<std::vector<u8>> GetTitleKek(std::string_view rights_id_hex) const;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::vector<u8>> keys_;
};

} // namespace nemu::core::crypto
