#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/crypto/key_store.hpp"
#include "core/filesystem/vfs.hpp"
#include <string>
#include <vector>
#include <optional>
#include <span>

namespace nemu::core::loader {

struct LoadedTitleInfo {
    vaddr_t base_address{0};
    vaddr_t entry_point{0};
    size_t total_size{0};
    std::string title_name;
    u64 title_id{0};
    bool is_nro{false};
};

class TitleLoader {
public:
    TitleLoader(crypto::KeyStore& key_store, filesystem::VirtualFileSystem& vfs);

    /// Universal loader that accepts .nro, .nso, .nsp, .xci, or .nca
    std::optional<LoadedTitleInfo> LoadTitle(
        const std::string& path_or_vpath,
        memory::VirtualMemory& vm,
        vaddr_t base_address = 0x0071000000ULL
    );

    /// Load from memory buffer with known format hint
    std::optional<LoadedTitleInfo> LoadFromMemory(
        std::span<const u8> data,
        memory::VirtualMemory& vm,
        std::string_view name_hint = "",
        vaddr_t base_address = 0x0071000000ULL
    );

private:
    crypto::KeyStore& key_store_;
    filesystem::VirtualFileSystem& vfs_;
};

} // namespace nemu::core::loader
