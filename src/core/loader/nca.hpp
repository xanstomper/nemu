#pragma once

#include "core/types.hpp"
#include "core/crypto/key_store.hpp"
#include "core/crypto/aes.hpp"
#include <span>
#include <vector>
#include <optional>
#include <array>
#include <string>

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
    None = 1, // raw
    Xts = 2,
    Ctr = 3,
    Bktr = 4,
};

enum class NcaSectionFsType : u8 {
    Invalid = 0,
    Pfs0 = 2,
    Romfs = 3,
};

struct NcaSectionInfo {
    u32 section_index{0};
    u64 offset{0}; // absolute offset of the section within the NCA file
    u64 size{0};
    NcaEncryptionType encryption_type{NcaEncryptionType::None};
    NcaSectionFsType fs_type{NcaSectionFsType::Invalid};
    u8 partition_type{0};
    // CTR sections: initial counter, base = section start.
    std::array<u8, 16> ctr{};
    // Window into the decrypted section stream where the payload lives:
    //   PFS0 sections : fs data begins at pfs0_offset (after the IVFC hash tree)
    //   RomFS sections: fs data begins at ivfc level-6 offset
    u64 data_window_offset{0};
    u64 data_window_size{0};
};

class NcaReader {
public:
    static constexpr u32 NCA3_MAGIC = 0x3341434E; // 'NCA3'
    static constexpr u32 NCA2_MAGIC = 0x3241434E; // 'NCA2'
    static constexpr u32 NCA0_MAGIC = 0x3041434E; // 'NCA0'

    // NCA3 header: 0x400 main header + 4 x 0x200 fs headers = 0xC00
    static constexpr size_t HEADER_SIZE = 0x400;
    static constexpr size_t FULL_HEADER_SIZE = 0xC00;
    static constexpr size_t BLOCK_SIZE = 0x200;   // media block
    static constexpr size_t FS_HEADER_SIZE = 0x200;

    NcaReader() = default;

    /// Parse an NCA container from memory.
    /// If encrypted, uses key_store to decrypt header and sections.
    bool Initialize(std::span<const u8> data, const crypto::KeyStore* key_store = nullptr);

    [[nodiscard]] u32 GetMagic() const noexcept { return magic_; }
    [[nodiscard]] NcaContentType GetContentType() const noexcept { return content_type_; }
    [[nodiscard]] u64 GetTitleId() const noexcept { return title_id_; }
    [[nodiscard]] u64 GetContentSize() const noexcept { return content_size_; }
    [[nodiscard]] u8 GetKeyGeneration() const noexcept { return effective_key_generation_; }
    [[nodiscard]] bool IsEncrypted() const noexcept { return is_encrypted_; }
    [[nodiscard]] bool HasRightsId() const noexcept { return has_rights_id_; }
    [[nodiscard]] std::array<u8, 16> GetRightsId() const noexcept { return rights_id_; }
    [[nodiscard]] std::string GetRightsIdHex() const;

    /// Check if section (0..3) exists
    [[nodiscard]] bool HasSection(u32 section_index) const;

    /// Retrieve metadata for section
    [[nodiscard]] std::optional<NcaSectionInfo> GetSectionInfo(u32 section_index) const;

    /// Extract and decrypt a section.
    /// Returns the raw decrypted stream of the WHOLE section (including IVFC
    /// hash layers). Callers that need the payload window should use
    /// ExtractSectionPayload().
    [[nodiscard]] std::optional<std::vector<u8>> ExtractSection(
        u32 section_index,
        const crypto::KeyStore* key_store = nullptr
    ) const;

    /// Extract only the payload window (PFS0 superblock offset for ExeFS,
    /// IVFC level-6 offset for RomFS). This is what loaders actually mount.
    [[nodiscard]] std::optional<std::vector<u8>> ExtractSectionPayload(
        u32 section_index,
        const crypto::KeyStore* key_store = nullptr
    ) const;

private:
    /// Resolve the 16-byte section key for a CTR section.
    std::optional<std::array<u8, 16>> ResolveSectionKey(const crypto::KeyStore& key_store) const;

    std::span<const u8> raw_data_;
    std::array<u8, FULL_HEADER_SIZE> decrypted_header_{};
    u32 magic_{0};
    NcaContentType content_type_{NcaContentType::Program};
    u64 title_id_{0};
    u64 content_size_{0};
    u8 crypto_type_{0};            // header 0x206
    u8 crypto_type2_{0};           // header 0x220
    u8 effective_key_generation_{0};
    u8 kaek_index_{0};
    bool is_encrypted_{false};
    bool has_rights_id_{false};
    std::array<u8, 16> rights_id_{};
    std::array<NcaSectionInfo, 4> sections_{};
    std::array<std::array<u8, 16>, 4> key_area_{}; // decrypted key area slots
};

} // namespace nemu::core::loader
