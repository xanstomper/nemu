#include "title_loader.hpp"
#include "nro.hpp"
#include "nso.hpp"
#include "pfs0.hpp"
#include "xci.hpp"
#include "nca.hpp"
#include "ncz.hpp"
#include "romfs.hpp"
#include "core/cpu/title_compat.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/kernel/svc.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <cstring>
#include <cstdlib>
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

    std::filesystem::path p(resolved_path);
    std::vector<u8> buffer;
    std::string title_filename = p.filename().string();

    if (NczDecompressor::IsSplitVolume(resolved_path)) {
        auto parts = NczDecompressor::GetSplitParts(resolved_path);
        if (parts.size() > 1) {
            NEMU_LOG_INFO("Loader", "Stitching multi-part split volume ({} parts found)", parts.size());
            size_t total_size = 0;
            for (const auto& part : parts) {
                total_size += std::filesystem::file_size(part);
            }
            buffer.resize(total_size);
            size_t offset = 0;
            for (const auto& part : parts) {
                std::ifstream part_file(part, std::ios::binary);
                const size_t sz = std::filesystem::file_size(part);
                part_file.read(reinterpret_cast<char*>(buffer.data() + offset), static_cast<std::streamsize>(sz));
                offset += sz;
            }
            title_filename = p.stem().string() + NczDecompressor::GetCanonicalExtension(resolved_path);
        }
    }

    if (buffer.empty()) {
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

        buffer.resize(static_cast<size_t>(file_size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    }

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

    return LoadFromMemory(buffer, vm, title_filename, base_address);
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
                    .size = loaded->total_size,
                    .exported_symbols = {},
                    .image = {}
                }
            }
        };
    }

    // 3a. Check for direct NCZ container (compressed NCA)
    if (NczDecompressor::IsNcz(data) || name_hint.ends_with(".ncz")) {
        NEMU_LOG_INFO("Loader", "Identified NCZ compressed container: {}", name_hint);
        auto decompressed = NczDecompressor::Decompress(data);
        if (decompressed) {
            std::string nca_hint = std::string(name_hint);
            if (nca_hint.ends_with(".ncz")) {
                nca_hint = nca_hint.substr(0, nca_hint.size() - 4) + ".nca";
            }
            return LoadFromMemory(*decompressed, vm, nca_hint, base_address);
        } else {
            NEMU_LOG_ERROR("Loader", "Failed to decompress NCZ container: {}", name_hint);
            return std::nullopt;
        }
    }

    // 3. Check for direct NCA (NCA3/2/0 at offset 0x200 or 0x00).
    //    Retail carts carry ENCRYPTED NCAs whose magic is only readable after
    //    header XTS decryption (NcaReader::Initialize). So also attempt NCA
    //    parse when the hint says .nca even if the raw magic isn't NCA* yet.
    bool is_nca = false;
    if (data.size() >= 0x400) {
        u32 nca_mag = 0;
        std::memcpy(&nca_mag, data.data() + 0x200, 4);
        if (nca_mag == NcaReader::NCA3_MAGIC || nca_mag == NcaReader::NCA2_MAGIC || nca_mag == NcaReader::NCA0_MAGIC) {
            is_nca = true;
        }
    }
    if (!is_nca && (name_hint.ends_with(".nca") || name_hint.ends_with(".ncz"))) {
        // Encrypted retail NCA — magic will validate inside NcaReader after
        // header decryption. Attempt it (NcaReader handles the keyed path).
        is_nca = true;
    }

    if (is_nca) {
        NEMU_LOG_INFO("Loader", "Identified NCA container: {}", name_hint);
        NcaReader nca;
        if (!nca.Initialize(data, &key_store_)) {
            NEMU_LOG_ERROR("Loader", "Failed to parse NCA");
            return std::nullopt;
        }

        // Mount RomFS (section 1) if present and not already mounted
        if (!vfs_.IsMounted("romfs:/") && nca.HasSection(1)) {
            auto romfs_data = nca.ExtractSectionPayload(1, &key_store_);
            if (romfs_data) {
                RomfsReader romfs;
                if (romfs.Initialize(*romfs_data)) {
                    std::error_code ec;
                    auto temp_dir = std::filesystem::temp_directory_path(ec);
                    if (ec || temp_dir.empty()) {
                        temp_dir = std::filesystem::current_path(ec) / "temp";
                    }
                    auto staging_dir = temp_dir / ("nemu_romfs_" + std::to_string(nca.GetTitleId()));
                    if (romfs.MountToVfs(vfs_, staging_dir, "romfs:/")) {
                        NEMU_LOG_INFO("Loader", "Mounted RomFS section 1 ({} files) to romfs:/", romfs.GetFiles().size());
                    }
                }
            }
        }

        // ExeFS lives at the PFS0 superblock offset inside the section (after
        // the IVFC hash layer), so extract the payload window, not the whole
        // section stream.
        auto exefs_opt = nca.ExtractSectionPayload(0, &key_store_);
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

    // 4. Check for XCI cartridge image (partition table HFS0 at offset 0x200).
    //    Unpack the game payloads (NSP/PFS0/NCAs) and load them recursively.
    if (XciArchive::IsXci(data) && name_hint.ends_with(".xci")) {
        XciArchive xci;
        if (xci.Initialize(data)) {
            std::vector<XciPayload> payloads;
            if (xci.UnpackGame(payloads)) {
                // Sort game payloads by size descending so the Program NCA (the
                // largest) is tried first — matches the NSP candidate ordering.
                std::stable_sort(payloads.begin(), payloads.end(),
                                 [](const XciPayload& a, const XciPayload& b) {
                                     return a.data.size() > b.data.size();
                                 });
                // Exclude known non-loadable entries before recursing.
                for (const auto& pl : payloads) {
                    if (pl.name == "main" || pl.name == "rtld") {
                        continue; // ExeFS handled elsewhere; skip partition-level.
                    }
                    // Register tickets (.tik) carried inside the cartridge so the
                    // title's key is available for NCA-body decryption even if the
                    // user's title.keys lacks this title.
                    if (pl.name.ends_with(".tik")) {
                        key_store_.RegisterTicket(pl.data, pl.name);
                        NEMU_LOG_INFO("Loader", "Registered ticket '{}' from XCI", pl.name);
                        continue; // tickets are not loadable titles
                    }
                    if (pl.name.ends_with(".cert") || pl.name.ends_with(".certs")) {
                        continue; // certificates are not loadable titles
                    }
                    auto loaded = LoadFromMemory(pl.data, vm, pl.name, base_address);
                    if (loaded) {
                        return loaded;
                    }
                }
                NEMU_LOG_ERROR("Loader", "No loadable payload found inside XCI cart");
                return std::nullopt;
            }
            NEMU_LOG_WARN("Loader", "XCI parsed but normal partition yielded no payloads");
        } else {
            NEMU_LOG_ERROR("Loader", "Failed to initialize XCI cartridge: {}", name_hint);
        }
        return std::nullopt;
    }

    // 5. Check for PFS0 container (.nsp or raw ExeFS)
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

        // NSP/NSZ package containing NCAs/NCZs: find candidate NCAs sorted by size descending
        std::vector<std::pair<std::string, size_t>> nca_candidates;
        for (const auto& f : pfs0.GetFiles()) {
            if (f.name.ends_with(".nca") || f.name.ends_with(".ncz")) {
                nca_candidates.push_back({f.name, f.size});
            }
        }
        std::sort(nca_candidates.begin(), nca_candidates.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        std::optional<LoadedTitleInfo> loaded_title;
        for (const auto& [nca_name, nca_size] : nca_candidates) {
            NEMU_LOG_INFO("Loader", "Attempting to load Program NCA/NCZ '{}' ({} bytes) in NSP package",
                          nca_name, nca_size);
            auto file_data = pfs0.OpenFile(nca_name);
            if (file_data) {
                std::vector<u8> decompressed_buf;
                std::span<const u8> target_data = *file_data;
                if (nca_name.ends_with(".ncz") || NczDecompressor::IsNcz(target_data)) {
                    auto dec = NczDecompressor::Decompress(target_data);
                    if (dec) {
                        decompressed_buf = std::move(*dec);
                        target_data = decompressed_buf;
                    }
                }

                auto loaded = LoadFromMemory(target_data, vm, nca_name, base_address);
                if (loaded) {
                    loaded_title = loaded;
                    break;
                }
            }
        }

        // If Program NCA was found, check if RomFS is mounted. If not mounted yet, search remaining NCAs for RomFS
        if (loaded_title && !vfs_.IsMounted("romfs:/")) {
            for (const auto& [nca_name, nca_size] : nca_candidates) {
                auto file_data = pfs0.OpenFile(nca_name);
                if (!file_data) continue;
                std::vector<u8> decompressed_buf;
                std::span<const u8> target_data = *file_data;
                if (nca_name.ends_with(".ncz") || NczDecompressor::IsNcz(target_data)) {
                    auto dec = NczDecompressor::Decompress(target_data);
                    if (dec) {
                        decompressed_buf = std::move(*dec);
                        target_data = decompressed_buf;
                    }
                }

                NcaReader nca;
                if (!nca.Initialize(target_data, &key_store_)) continue;

                for (u32 s : {1u, 0u}) {
                    if (nca.HasSection(s)) {
                        auto romfs_data = nca.ExtractSectionPayload(s, &key_store_);
                        if (romfs_data) {
                            RomfsReader romfs;
                            if (romfs.Initialize(*romfs_data)) {
                                std::error_code ec;
                                auto temp_dir = std::filesystem::temp_directory_path(ec);
                                if (ec || temp_dir.empty()) {
                                    temp_dir = std::filesystem::current_path(ec) / "temp";
                                }
                                auto staging_dir = temp_dir / ("nemu_romfs_" + std::to_string(loaded_title->title_id != 0 ? loaded_title->title_id : nca.GetTitleId()));
                                if (romfs.MountToVfs(vfs_, staging_dir, "romfs:/")) {
                                    NEMU_LOG_INFO("Loader", "Mounted RomFS from NCA '{}' section {} ({} files) to romfs:/",
                                                  nca_name, s, romfs.GetFiles().size());
                                    break;
                                }
                            }
                        }
                    }
                }
                if (vfs_.IsMounted("romfs:/")) break;
            }
        }

        if (loaded_title) {
            return loaded_title;
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
                        if (f.name.ends_with(".nca") || f.name.ends_with(".ncz")) {
                            nca_candidates.push_back({f.name, f.size});
                        }
                    }
                    std::sort(nca_candidates.begin(), nca_candidates.end(),
                              [](const auto& a, const auto& b) { return a.second > b.second; });

                    std::optional<LoadedTitleInfo> loaded_title;
                    for (const auto& [nca_name, nca_size] : nca_candidates) {
                        auto file_data = secure_hfs0.OpenFile(nca_name);
                        if (file_data) {
                            std::vector<u8> decompressed_buf;
                            std::span<const u8> target_data = *file_data;
                            if (nca_name.ends_with(".ncz") || NczDecompressor::IsNcz(target_data)) {
                                auto dec = NczDecompressor::Decompress(target_data);
                                if (dec) {
                                    decompressed_buf = std::move(*dec);
                                    target_data = decompressed_buf;
                                }
                            }

                            auto loaded = LoadFromMemory(target_data, vm, nca_name, base_address);
                            if (loaded) {
                                loaded_title = loaded;
                                break;
                            }
                        }
                    }

                    if (loaded_title && !vfs_.IsMounted("romfs:/")) {
                        for (const auto& [nca_name, nca_size] : nca_candidates) {
                            auto file_data = secure_hfs0.OpenFile(nca_name);
                            if (!file_data) continue;
                            std::vector<u8> decompressed_buf;
                            std::span<const u8> target_data = *file_data;
                            if (nca_name.ends_with(".ncz") || NczDecompressor::IsNcz(target_data)) {
                                auto dec = NczDecompressor::Decompress(target_data);
                                if (dec) {
                                    decompressed_buf = std::move(*dec);
                                    target_data = decompressed_buf;
                                }
                            }

                            NcaReader nca;
                            if (!nca.Initialize(target_data, &key_store_)) continue;

                            for (u32 s : {1u, 0u}) {
                                if (nca.HasSection(s)) {
                                    auto romfs_data = nca.ExtractSection(s, &key_store_);
                                    if (romfs_data) {
                                        RomfsReader romfs;
                                        if (romfs.Initialize(*romfs_data)) {
                                            std::error_code ec;
                                            auto temp_dir = std::filesystem::temp_directory_path(ec);
                                            if (ec || temp_dir.empty()) {
                                                temp_dir = std::filesystem::current_path(ec) / "temp";
                                            }
                                            auto staging_dir = temp_dir / ("nemu_romfs_" + std::to_string(loaded_title->title_id != 0 ? loaded_title->title_id : nca.GetTitleId()));
                                            if (romfs.MountToVfs(vfs_, staging_dir, "romfs:/")) {
                                                NEMU_LOG_INFO("Loader", "Mounted RomFS from NCA '{}' section {} ({} files) to romfs:/",
                                                              nca_name, s, romfs.GetFiles().size());
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            if (vfs_.IsMounted("romfs:/")) break;
                        }
                    }

                    if (loaded_title) {
                        return loaded_title;
                    }
                }
            }
        }
    }

    NEMU_LOG_ERROR("Loader", "Unrecognized or unsupported executable/container format for '{}'", name_hint);
    return std::nullopt;
}

namespace {

// Extract (init_array, count) from a module image's .dynamic.
bool FindInitArray(const std::vector<u8>& img, u64& init_array, u64& init_arraysz) {
    if (img.size() < 0x20) return false;
    size_t mod0 = std::string_view::npos;
    for (size_t i = 0; i + 4 <= img.size(); i += 4) {
        u32 magic = 0;
        std::memcpy(&magic, img.data() + i, 4);
        if (magic == 0x30444F4D) { mod0 = i; break; }
    }
    if (mod0 == std::string_view::npos) return false;
    s32 dyn_rel = 0;
    std::memcpy(&dyn_rel, img.data() + mod0 + 4, sizeof(s32));
    size_t p = mod0 + static_cast<size_t>(dyn_rel);
    while (p + 16 <= img.size()) {
        s64 t = 0; u64 v = 0;
        std::memcpy(&t, img.data() + p, 8);
        std::memcpy(&v, img.data() + p + 8, 8);
        p += 16;
        if (t == 0) break;
        if (t == 25) init_array = v;
        else if (t == 27) init_arraysz = v;
    }
    return init_array != 0 && init_arraysz != 0;
}

} // namespace

void TitleLoader::RunModuleInitArrays(
    memory::VirtualMemory& vm,
    const std::vector<LoadedModuleInfo>& modules,
    const std::function<void(cpu::CpuState&, u32)>& svc_dispatch
) {
    constexpr vaddr_t kInitReturnSentinel = 0x000000001CE11000ULL;
    constexpr vaddr_t kInitStackTop = 0x000000006FF00000ULL;
    constexpr size_t kInitStackSize = 0x40000;
    if (!vm.IsValidAddress(kInitStackTop - 0x1000, 0x1000)) {
        vm.Map(kInitStackTop - kInitStackSize, kInitStackSize,
               memory::MemoryPermission::ReadWrite);
    }

    constexpr u64 kPerFunctionBudget = 5'000'000;
    size_t total_run = 0, total_failed = 0;

    for (const auto& m : modules) {
        if (m.image.empty()) continue;
        u64 init_array = 0, init_arraysz = 0;
        if (!FindInitArray(m.image, init_array, init_arraysz)) continue;
        const size_t count = static_cast<size_t>(init_arraysz / 8);
        NEMU_LOG_INFO("Loader", "Module '{}' init_array: {} entries at +{:#x}",
                      m.name, count, init_array);

        for (size_t i = 0; i < count; ++i) {
            const size_t slot = static_cast<size_t>(init_array) + i * 8;
            if (slot + 8 > m.image.size()) break;
            u64 fn_rel = 0;
            std::memcpy(&fn_rel, m.image.data() + slot, 8);
            if (fn_rel == 0) continue;
            vaddr_t fn = m.base_address + fn_rel;
            const u64 in_guest = vm.IsValidAddress(m.base_address + slot, 8)
                               ? vm.Read64(m.base_address + slot) : 0;
            if (in_guest != 0 && in_guest != fn_rel) fn = in_guest;
            if (!vm.IsValidAddress(fn, 4)) continue;

            cpu::CpuState cpu;
            cpu.pc = fn;
            cpu.sp = kInitStackTop;
            cpu.SetX(0, m.base_address);
            cpu.SetX(30, kInitReturnSentinel);
            cpu::Interpreter interp(cpu, vm);
            if (svc_dispatch) {
                interp.SetSvcHandler(svc_dispatch);
            }
            bool ok = true;
            u64 fault_streak = 0;
            for (u64 s = 0; s < kPerFunctionBudget; ++s) {
                if (cpu.pc == kInitReturnSentinel || cpu.halted) break;
                const auto res = interp.Step();
                if (res == cpu::StepResult::MemoryFault ||
                    res == cpu::StepResult::UndefinedInstruction) {
                    if (++fault_streak > 64) { ok = false; break; } // runaway, abort
                } else {
                    fault_streak = 0;
                }
            }
            if (ok) ++total_run; else ++total_failed;
        }
    }
    NEMU_LOG_INFO("Loader", "Module init_array execution: {} ok, {} faulted", total_run, total_failed);
}

std::optional<LoadedTitleInfo> TitleLoader::LoadExeFS(
    const Pfs0Archive& exefs,
    memory::VirtualMemory& vm,
    std::string_view name_hint,
    u64 title_id,
    vaddr_t base_address
) {
    // If title_id is not provided, try to extract it from main.npdm
    if (title_id == 0 && exefs.HasFile("main.npdm")) {
        auto npdm = exefs.OpenFile("main.npdm");
        if (npdm && npdm->size() >= 0x70) {
            u32 meta_magic = 0;
            std::memcpy(&meta_magic, npdm->data(), 4);
            if (meta_magic == 0x4154454D) { // 'META'
                u32 aci0_offset = 0;
                std::memcpy(&aci0_offset, npdm->data() + 0x40, 4);
                if (aci0_offset + 0x18 <= npdm->size()) {
                    u32 aci0_magic = 0;
                    std::memcpy(&aci0_magic, npdm->data() + aci0_offset, 4);
                    if (aci0_magic == 0x30494341) { // 'ACI0'
                        std::memcpy(&title_id, npdm->data() + aci0_offset + 0x10, sizeof(u64));
                        NEMU_LOG_INFO("Loader", "Parsed Title ID 0x{:016X} from main.npdm", title_id);
                    }
                }
            }
        }
    }

    std::string title_display_name = std::string(name_hint);
    if (title_id != 0) {
        const auto* compat = cpu::FindTitleCompat(title_id);
        if (compat) {
            NEMU_LOG_INFO("Loader", "Matched Title ID 0x{:016X} to compat database: '{}'",
                          title_id, compat->name);
            if (title_display_name.empty()) {
                title_display_name = std::string(compat->name);
            }
        }
    }

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

        auto loaded = NsoLoader::Load(*mod_data, vm, curr_base, &vfs_, title_id);
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
            .size = loaded->total_size,
            .exported_symbols = loaded->exported_symbols,
            .image = std::move(loaded->image)
        });

        // Primary entry selection.
        //
        // Overridable with NEMU_BOOT_ENTRY=main|rtld; the default is "main".
        //
        //   "main" (default) boots the game's own crt0 directly. This is what
        //     every mature Switch emulator does: Ryujinx (both the classic tree
        //     and the current `qlaunch` tree) sets the process entry point to the
        //     first NSO in the ExeFS and performs all relocations in the loader
        //     (`ProcessLoaderHelper.LoadNsos` -> `LoadIntoMemory`), and Eden
        //     (yuzu) does the same via `AppLoader_NSO::Load`. None of them hand
        //     rtld a kernel-installed ldr module map, and none of them boot rtld
        //     directly. NEMU already applies every module's RELA relocations and
        //     resolves cross-module imports at load time, so rtld's relocation
        //     work is redundant here.
        //
        //   "rtld" is kept only for bring-up comparison. Booting rtld directly
        //     stalls: rtld derives its own load base from a *module list*
        //     structure the real kernel hands it, and NEMU never installs one,
        //     so rtld relocates itself to a garbage base (observed:
        //     0x1871_0000_00 on Terraria) and every write from there faults.
        const char* entry_env = std::getenv("NEMU_BOOT_ENTRY");
        const bool prefer_rtld = (entry_env != nullptr) && (std::string_view(entry_env) == "rtld");

        if (mod_name == "rtld" && prefer_rtld) {
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

    // Cross-module symbol resolution: build a global name -> guest-address map
    // from every module's exported symbols (base + module-relative value), then
    // resolve each module's GLOB_DAT / JUMP_SLOT imports (strdup, longjmp,
    // stdout, C++ vtables/strings, __rel_* markers, ...) against it. Without
    // this, indirect `BR X17`-style calls through the GOT hit unmapped garbage.
    {
        std::unordered_map<std::string, u64> global_symbols;
        for (const auto& m : loaded_modules) {
            for (const auto& [name, val] : m.exported_symbols) {
                global_symbols[name] = m.base_address + val;
            }
        }
        if (!global_symbols.empty()) {
            for (auto& m : loaded_modules) {
                if (m.image.empty()) continue;
                NsoLoader::ResolveSymbolImports(vm, m.base_address, m.image, global_symbols);
            }
            NEMU_LOG_INFO("Loader", "Linked {} symbols across {} modules", global_symbols.size(), loaded_modules.size());
        }
    }

    // Run each module's .init_array (DT_INIT_ARRAY / DT_INIT_ARRAYSZ) in load
    // order, exactly like the real rtld does before transferring control to
    // the primary entry. Commercial SDK modules register their allocators,
    // service globals, and C++ static constructors here; skipping this leaves
    // nn::os allocator callbacks null so every operator new returns null and
    // the guest collapses to a PC=0 spin (verified on Terraria).
    // NOTE: executed by the Emulator (RunModuleInitArrays) after this returns,
    // where the process/thread/SVC dispatch exist.

    size_t total_size = static_cast<size_t>(curr_base - base_address);

    return LoadedTitleInfo{
        .base_address = base_address,
        .entry_point = primary_entry,
        .total_size = total_size,
        .title_name = title_display_name,
        .title_id = title_id,
        .is_nro = false,
        .modules = std::move(loaded_modules)
    };
}

} // namespace nemu::core::loader
