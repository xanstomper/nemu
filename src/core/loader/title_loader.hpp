#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/crypto/key_store.hpp"
#include "core/filesystem/vfs.hpp"
#include <string>
#include <vector>
#include <optional>
#include <span>
#include <unordered_map>
#include <functional>

namespace nemu::core::cpu { struct CpuState; }

namespace nemu::core::loader {

struct LoadedModuleInfo {
    std::string name;
    vaddr_t base_address{0};
    vaddr_t entry_point{0};
    size_t size{0};

    /// Exported dynamic symbols (name -> module-relative value).
    std::unordered_map<std::string, u64> exported_symbols;
    /// Flat module image retained for cross-module import resolution.
    std::vector<u8> image;
};

struct LoadedTitleInfo {
    vaddr_t base_address{0};
    vaddr_t entry_point{0};
    size_t total_size{0};
    std::string title_name;
    u64 title_id{0};
    bool is_nro{false};
    std::vector<LoadedModuleInfo> modules;
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

    /// Run each loaded module's .init_array (DT_INIT_ARRAY) in load order.
    /// Requires the emulator's SVC dispatch for init functions that call the
    /// kernel (sm:/fs:/mem etc.).
    void RunModuleInitArrays(
        memory::VirtualMemory& vm,
        const std::vector<LoadedModuleInfo>& modules,
        const std::function<void(cpu::CpuState&, u32)>& svc_dispatch = {}
    );

    /// Load modular ExeFS containing rtld, main, subsdk*, sdk
    std::optional<LoadedTitleInfo> LoadExeFS(
        const class Pfs0Archive& exefs,
        memory::VirtualMemory& vm,
        std::string_view name_hint = "",
        u64 title_id = 0,
        vaddr_t base_address = 0x0071000000ULL
    );

private:
    crypto::KeyStore& key_store_;
    filesystem::VirtualFileSystem& vfs_;
};

} // namespace nemu::core::loader
