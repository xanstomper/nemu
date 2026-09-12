#include "title_loader.hpp"
#include "nro.hpp"
#include "nso.hpp"
#include "pfs0.hpp"
#include "nca.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace nemu::core::loader {

TitleLoader::TitleLoader(crypto::KeyStore& key_store, filesystem::VirtualFileSystem& vfs)
    : key_store_(key_store), vfs_(vfs) {}

std::optional<LoadedTitleInfo> TitleLoader::LoadTitle(
    const std::string& path_or_vpath,
    memory::VirtualMemory& vm,
    vaddr_t base_address
) {
    std::string resolved_path = path_or_vpath;
    auto vfs_resolved = vfs_.ResolvePath(path_or_vpath);
    if (vfs_resolved) {
        // Explicit path -> string conversion (required by MinGW/Windows where
        // the implicit std::filesystem::path::operator std::string is absent).
        resolved_path = vfs_resolved->string();
    }

    std::ifstream file(resolved_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NEMU_LOG_ERROR("Loader", "Could not open target title: {}", resolved_path);
        return std::nullopt;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        NEMU_LOG_ERROR("Loader", "Target title is empty: {}", resolved_path);
        return std::nullopt;
    }

    std::vector<u8> buffer(static_cast<size_t>(file_size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    std::filesystem::path p(resolved_path);
    // If the title is in a directory, scan the directory for any .tik files
    try {
        auto dir = p.parent_path();
        if (!dir.empty() && std::filesystem::exists(dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".tik") {
                    key_store_.LoadTicketFromFile(entry.path().string());
                }
            }
        }
    } catch (...) {
        // Best-effort directory scan
    }

    return LoadFromMemory(buffer, vm, p.filename().string(), base_address);
}

std::optional<LoadedTitleInfo> TitleLoader::LoadFromMemory(
    std::span<const u8> data,
    memory::VirtualMemory& vm,
    std::string_view name_hint,
    vaddr_t base_address
) {
    if (data.size() < 16) {
        NEMU_LOG_ERROR("Loader", "Data buffer too small to identify format");
        return std::nullopt;
    }

    // 1. Check for NRO0 magic at offset 0x10
    if (data.size() >= 0x20 &&
        data[0x10] == 'N' && data[0x11] == 'R' && data[0x12] == 'O' && data[0x13] == '0') {
        NEMU_LOG_INFO("Loader", "Identified NRO format: {}", name_hint);
        auto loaded = NroLoader::Load(data, vm, base_address);
        if (!loaded) return std::nullopt;
        return LoadedTitleInfo{
            .base_address = loaded->load_address,
            .entry_point = loaded->entry_point,
            .total_size = loaded->total_mapped_size,
            .title_name = std::string(name_hint),
            .title_id = 0,
            .is_nro = true,
            .modules = {}
        };
    }

    // 2. Check for NSO0 magic at offset 0x00
    u32 magic_0 = 0;
    std::memcpy(&magic_0, data.data(), 4);
    if (magic_0 == NsoLoader::NSO_MAGIC) {
        NEMU_LOG_INFO("Loader", "Identified raw NSO format: {}", name_hint);
        auto loaded = NsoLoader::Load(data, vm, base_address);
        if (!loaded) return std::nullopt;
        return LoadedTitleInfo{
            .base_address = loaded->base_address,
            .entry_point = loaded->entry_point,
            .total_size = loaded->total_size,
            .title_name = std::string(name_hint),
            .title_id = 0,
            .is_nro = false,
            .modules = {
                LoadedModuleInfo{
                    .name = std::string(name_hint),
                    .base_address = loaded->base_address,
                    .entry_point = loaded->entry_point,
                    .size = loaded->total_size
                }
            }
        };
    }

    // 3. Check for direct NCA (NCA3/2/0 at offset 0x200 or 0x00)
    bool is_nca = false;
    if (data.size() >= 0x400) {
        u32 nca_mag = 0;
        std::memcpy(&nca_mag, data.data() + 0x200, 4);
        if (nca_mag == NcaReader::NCA3_MAGIC || nca_mag == NcaReader::NCA2_MAGIC || nca_mag == NcaReader::NCA0_MAGIC) {
            is_nca = true;
        }
    }

    if (is_nca) {
        NEMU_LOG_INFO("Loader", "Identified NCA container: {}", name_hint);
        NcaReader nca;
        if (!nca.Initialize(data, &key_store_)) {
            NEMU_LOG_ERROR("Loader", "Failed to parse NCA");
            return std::nullopt;
        }

        auto exefs_opt = nca.ExtractSection(0, &key_store_);
        if (!exefs_opt) {
            NEMU_LOG_ERROR("Loader", "NCA does not contain valid ExeFS section 0");
            return std::nullopt;
        }

        Pfs0Archive exefs;
        if (!exefs.Initialize(*exefs_opt)) {
            NEMU_LOG_ERROR("Loader", "Failed to parse ExeFS PFS0 archive");
            return std::nullopt;
        }

        return LoadExeFS(exefs, vm, name_hint, nca.GetTitleId(), base_address);
    }

    // 4. Check for PFS0 container (.nsp or raw ExeFS)
    if (magic_0 == Pfs0Archive::PFS0_MAGIC) {
        NEMU_LOG_INFO("Loader", "Identified PFS0 container (.nsp or ExeFS): {}", name_hint);
        Pfs0Archive pfs0;
        if (!pfs0.Initialize(data)) {
            NEMU_LOG_ERROR("Loader", "Failed to initialize PFS0 container");
            return std::nullopt;
        }

        // Direct ExeFS containing 'main' or 'rtld'
        if (pfs0.HasFile("main") || pfs0.HasFile("rtld")) {
            return LoadExeFS(pfs0, vm, name_hint, 0, base_address);
        }

        // NSP package: First find and register all tickets (.tik)
        for (const auto& f : pfs0.GetFiles()) {
            if (f.name.ends_with(".tik")) {
                auto tik_data = pfs0.OpenFile(f.name);
                if (tik_data) {
                    bool reg_ok = key_store_.RegisterTicket(*tik_data, f.name);
                    NEMU_LOG_INFO("Loader", "Parsed ticket '{}' in NSP package: {}",
                                  f.name, reg_ok ? "Registered successfully" : "Failed to parse");
                }
            }
        }

        // NSP package containing NCAs: find candidate NCAs sorted by size descending
        std::vector<std::pair<std::string, size_t>> nca_candidates;
        for (const auto& f : pfs0.GetFiles()) {
            if (f.name.ends_with(".nca")) {
                nca_candidates.push_back({f.name, f.size});
            }
        }
        std::sort(nca_candidates.begin(), nca_candidates.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        for (const auto& [nca_name, nca_size] : nca_candidates) {
            NEMU_LOG_INFO("Loader", "Attempting to load Program NCA '{}' ({} bytes) in NSP package",
                          nca_name, nca_size);
            auto nca_data = pfs0.OpenFile(nca_name);
            if (nca_data) {
                auto loaded = LoadFromMemory(*nca_data, vm, nca_name, base_address);
                if (loaded) {
                    return loaded;
                }
            }
        }
    }

    // 5. Check for XCI container (HFS0 at 0xF000 or HFS0 magic at 0x00)
    size_t hfs0_offset = 0;
    if (magic_0 == Pfs0Archive::HFS0_MAGIC) {
        hfs0_offset = 0;
    } else if (data.size() > 0xF200) {
        u32 xci_mag = 0;
        std::memcpy(&xci_mag, data.data() + 0xF000, 4);
        if (xci_mag == Pfs0Archive::HFS0_MAGIC) {
            hfs0_offset = 0xF000;
        }
    }

    if (hfs0_offset != 0 || (magic_0 == Pfs0Archive::HFS0_MAGIC)) {
        NEMU_LOG_INFO("Loader", "Identified XCI/HFS0 container: {}", name_hint);
        Pfs0Archive root_hfs0;
        if (root_hfs0.Initialize(data.subspan(hfs0_offset))) {
            auto secure_opt = root_hfs0.OpenFile("secure");
            if (secure_opt) {
                Pfs0Archive secure_hfs0;
                if (secure_hfs0.Initialize(*secure_opt)) {
                    // Check for tickets in secure partition
                    for (const auto& f : secure_hfs0.GetFiles()) {
                        if (f.name.ends_with(".tik")) {
                            auto tik_data = secure_hfs0.OpenFile(f.name);
                            if (tik_data) {
                                key_store_.RegisterTicket(*tik_data, f.name);
                            }
                        }
                    }

                    std::vector<std::pair<std::string, size_t>> nca_candidates;
                    for (const auto& f : secure_hfs0.GetFiles()) {
                        if (f.name.ends_with(".nca")) {
                            nca_candidates.push_back({f.name, f.size});
                        }
                    }
                    std::sort(nca_candidates.begin(), nca_candidates.end(),
                              [](const auto& a, const auto& b) { return a.second > b.second; });

                    for (const auto& [nca_name, nca_size] : nca_candidates) {
                        auto nca_data = secure_hfs0.OpenFile(nca_name);
                        if (nca_data) {
                            auto loaded = LoadFromMemory(*nca_data, vm, nca_name, base_address);
                            if (loaded) {
                                return loaded;
                            }
                        }
                    }
                }
            }
        }
    }

    NEMU_LOG_ERROR("Loader", "Unrecognized or unsupported executable/container format for '{}'", name_hint);
    return std::nullopt;
}

std::optional<LoadedTitleInfo> TitleLoader::LoadExeFS(
    const Pfs0Archive& exefs,
    memory::VirtualMemory& vm,
    std::string_view name_hint,
    u64 title_id,
    vaddr_t base_address
) {
    // Determine module loading order: rtld, main, subsdk0..subsdk9, sdk
    std::vector<std::string> load_order;
    if (exefs.HasFile("rtld")) load_order.push_back("rtld");
    if (exefs.HasFile("main")) load_order.push_back("main");

    for (int i = 0; i < 10; ++i) {
        std::string sub = "subsdk" + std::to_string(i);
        if (exefs.HasFile(sub)) {
            load_order.push_back(sub);
        }
    }
    if (exefs.HasFile("sdk")) load_order.push_back("sdk");

    // Include any other files containing NSO0 magic
    for (const auto& f : exefs.GetFiles()) {
        if (std::find(load_order.begin(), load_order.end(), f.name) == load_order.end()) {
            auto file_bytes = exefs.OpenFile(f.name);
            if (file_bytes && file_bytes->size() >= 4) {
                u32 magic = 0;
                std::memcpy(&magic, file_bytes->data(), 4);
                if (magic == NsoLoader::NSO_MAGIC) {
                    load_order.push_back(f.name);
                }
            }
        }
    }

    if (load_order.empty()) {
        NEMU_LOG_ERROR("Loader", "ExeFS does not contain any recognizable NSO binaries");
        return std::nullopt;
    }

    vaddr_t curr_base = base_address;
    vaddr_t primary_entry = 0;
    std::vector<LoadedModuleInfo> loaded_modules;

    for (const auto& mod_name : load_order) {
        auto mod_data = exefs.OpenFile(mod_name);
        if (!mod_data) continue;

        // Align module base address to 64 KiB
        constexpr u64 MODULE_ALIGN = 0x10000;
        curr_base = (curr_base + MODULE_ALIGN - 1) & ~(MODULE_ALIGN - 1);

        auto loaded = NsoLoader::Load(*mod_data, vm, curr_base);
        if (!loaded) {
            NEMU_LOG_ERROR("Loader", "Failed to load module '{}' at 0x{:016X}", mod_name, curr_base);
            continue;
        }

        NEMU_LOG_INFO("Loader", "Loaded NSO module '{}' at [0x{:016X} - 0x{:016X}], entry: 0x{:016X}",
                      mod_name, loaded->base_address, loaded->base_address + loaded->total_size, loaded->entry_point);

        loaded_modules.push_back(LoadedModuleInfo{
            .name = mod_name,
            .base_address = loaded->base_address,
            .entry_point = loaded->entry_point,
            .size = loaded->total_size
        });

        if (mod_name == "rtld") {
            primary_entry = loaded->entry_point;
        } else if (mod_name == "main" && primary_entry == 0) {
            primary_entry = loaded->entry_point;
        }

        curr_base += loaded->total_size;
    }

    if (loaded_modules.empty()) {
        NEMU_LOG_ERROR("Loader", "No modules successfully mapped from ExeFS");
        return std::nullopt;
    }

    if (primary_entry == 0) {
        primary_entry = loaded_modules.front().entry_point;
    }

    size_t total_size = static_cast<size_t>(curr_base - base_address);

    return LoadedTitleInfo{
        .base_address = base_address,
        .entry_point = primary_entry,
        .total_size = total_size,
        .title_name = std::string(name_hint),
        .title_id = title_id,
        .is_nro = false,
        .modules = std::move(loaded_modules)
    };
}

} // namespace nemu::core::loader
