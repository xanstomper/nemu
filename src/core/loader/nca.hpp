#pragma once

#include "core/types.hpp"
#include "core/crypto/key_store.hpp"
#include "core/crypto/aes.hpp"
#include <span>
#include <vector>
#include <optional>
#include <array>

namespace nemu::core::loader {

enum class NcaContentType : u8 {
    Program = 0,
    Meta = 1,
    Control = 2,
    Manual = 3,
    Data = 4,
    PublicData = 5,
};

enum class NcaEncryptionType : u8 {
    Auto = 1,
    None = 2,
    Xts = 3,
    Ctr = 4,
    Bktr = 5,
};

struct NcaSectionInfo {
    u32 section_index{0};
    u64 offset{0};
    u64 size{0};
    NcaEncryptionType encryption_type{NcaEncryptionType::None};
    std::array<u8, 16> ctr{};
};

class NcaReader {
public:
    static constexpr u32 NCA3_MAGIC = 0x3341434E; // 'NCA3'
    static constexpr u32 NCA2_MAGIC = 0x3241434E; // 'NCA2'
    static constexpr u32 NCA0_MAGIC = 0x3041434E; // 'NCA0'
    static constexpr size_t HEADER_SIZE = 0x400;  // 1024 bytes
    static constexpr size_t BLOCK_SIZE = 0x200;   // 512 bytes

    NcaReader() = default;

    /// Parse an NCA container from memory
    /// If encrypted, uses key_store to decrypt header and sections
    bool Initialize(std::span<const u8> data, const crypto::KeyStore* key_store = nullptr);

    [[nodiscard]] u32 GetMagic() const noexcept { return magic_; }
    [[nodiscard]] NcaContentType GetContentType() const noexcept { return content_type_; }
    [[nodiscard]] u64 GetTitleId() const noexcept { return title_id_; }
    [[nodiscard]] u64 GetContentSize() const noexcept { return content_size_; }
    [[nodiscard]] u8 GetKeyGeneration() const noexcept { return key_generation_; }
    [[nodiscard]] bool IsEncrypted() const noexcept { return is_encrypted_; }

    /// Check if section (0..3) exists
    [[nodiscard]] bool HasSection(u32 section_index) const;

    /// Retrieve metadata for section
    [[nodiscard]] std::optional<NcaSectionInfo> GetSectionInfo(u32 section_index) const;

    /// Extract and decrypt section (0 = ExeFS, 1 = RomFS usually)
    [[nodiscard]] std::optional<std::vector<u8>> ExtractSection(
        u32 section_index,
        const crypto::KeyStore* key_store = nullptr
    ) const;

private:
    std::span<const u8> raw_data_;
    std::array<u8, HEADER_SIZE> decrypted_header_{};
    u32 magic_{0};
    NcaContentType content_type_{NcaContentType::Program};
    u64 title_id_{0};
    u64 content_size_{0};
    u8 key_generation_{0};
    u8 kaek_index_{0};
    bool is_encrypted_{false};
    std::array<NcaSectionInfo, 4> sections_{};
    std::array<std::array<u8, 16>, 4> key_area_{};
};

} // namespace nemu::core::loader
