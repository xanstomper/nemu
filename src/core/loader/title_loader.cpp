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
            .is_nro = true
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
            .is_nro = false
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

        auto main_opt = exefs.OpenFile("main");
        if (!main_opt) {
            NEMU_LOG_ERROR("Loader", "ExeFS missing 'main' executable");
            return std::nullopt;
        }

        auto loaded = NsoLoader::Load(*main_opt, vm, base_address);
        if (!loaded) return std::nullopt;

        return LoadedTitleInfo{
            .base_address = loaded->base_address,
            .entry_point = loaded->entry_point,
            .total_size = loaded->total_size,
            .title_name = std::string(name_hint),
            .title_id = nca.GetTitleId(),
            .is_nro = false
        };
    }

    // 4. Check for PFS0 container (.nsp or raw ExeFS)
    if (magic_0 == Pfs0Archive::PFS0_MAGIC) {
        NEMU_LOG_INFO("Loader", "Identified PFS0 container (.nsp or ExeFS): {}", name_hint);
        Pfs0Archive pfs0;
        if (!pfs0.Initialize(data)) {
            NEMU_LOG_ERROR("Loader", "Failed to initialize PFS0 container");
            return std::nullopt;
        }

        // Direct ExeFS containing 'main'
        if (pfs0.HasFile("main")) {
            auto main_opt = pfs0.OpenFile("main");
            auto loaded = NsoLoader::Load(*main_opt, vm, base_address);
            if (!loaded) return std::nullopt;
            return LoadedTitleInfo{
                .base_address = loaded->base_address,
                .entry_point = loaded->entry_point,
                .total_size = loaded->total_size,
                .title_name = std::string(name_hint),
                .title_id = 0,
                .is_nro = false
            };
        }

        // NSP package containing NCAs: find the largest .nca (typically Program NCA)
        std::string chosen_nca;
        size_t max_size = 0;
        for (const auto& f : pfs0.GetFiles()) {
            if (f.name.ends_with(".nca") && f.size > max_size) {
                max_size = f.size;
                chosen_nca = f.name;
            }
        }

        if (!chosen_nca.empty()) {
            NEMU_LOG_INFO("Loader", "Found Program NCA '{}' in NSP package", chosen_nca);
            auto nca_data = pfs0.OpenFile(chosen_nca);
            if (nca_data) {
                return LoadFromMemory(*nca_data, vm, chosen_nca, base_address);
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
                    std::string chosen_nca;
                    size_t max_size = 0;
                    for (const auto& f : secure_hfs0.GetFiles()) {
                        if (f.name.ends_with(".nca") && f.size > max_size) {
                            max_size = f.size;
                            chosen_nca = f.name;
                        }
                    }
                    if (!chosen_nca.empty()) {
                        auto nca_data = secure_hfs0.OpenFile(chosen_nca);
                        if (nca_data) {
                            return LoadFromMemory(*nca_data, vm, chosen_nca, base_address);
                        }
                    }
                }
            }
        }
    }

    NEMU_LOG_ERROR("Loader", "Unrecognized or unsupported executable/container format for '{}'", name_hint);
    return std::nullopt;
}

} // namespace nemu::core::loader
