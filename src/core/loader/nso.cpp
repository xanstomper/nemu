#include "nso.hpp"
#include "patch_manager.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace nemu::core::loader {

bool NsoLoader::DecompressLZ4(std::span<const u8> src, std::span<u8> dst) {
    size_t ip = 0;
    size_t op = 0;
    const size_t src_size = src.size();
    const size_t dst_size = dst.size();

    while (ip < src_size) {
        u8 token = src[ip++];
        size_t literal_len = (token >> 4);
        if (literal_len == 15) {
            while (ip < src_size) {
                u8 b = src[ip++];
                literal_len += b;
                if (b != 255) break;
            }
        }

        if (ip + literal_len > src_size || op + literal_len > dst_size) {
            return false;
        }

        std::memcpy(dst.data() + op, src.data() + ip, literal_len);
        ip += literal_len;
        op += literal_len;

        if (ip >= src_size) {
            break;
        }

        if (ip + 2 > src_size) {
            return false;
        }
        u16 match_offset = static_cast<u16>(src[ip]) | (static_cast<u16>(src[ip + 1]) << 8);
        ip += 2;

        if (match_offset == 0 || match_offset > op) {
            return false;
        }

        size_t match_len = (token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            while (ip < src_size) {
                u8 b = src[ip++];
                match_len += b;
                if (b != 255) break;
            }
        }

        if (op + match_len > dst_size) {
            return false;
        }

        for (size_t i = 0; i < match_len; ++i) {
            dst[op] = dst[op - match_offset];
            op++;
        }
    }
    return true;
}

std::optional<NsoLoadedImage> NsoLoader::Load(
    std::span<const u8> data,
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    filesystem::VirtualFileSystem* vfs,
    u64 title_id,
    bool leave_relocations
) {
    if (data.size() < sizeof(NsoHeader)) {
        NEMU_LOG_ERROR("Loader", "NSO data too small for header ({} bytes)", data.size());
        return std::nullopt;
    }

    const auto* hdr = reinterpret_cast<const NsoHeader*>(data.data());
    if (hdr->magic != NSO_MAGIC) {
        NEMU_LOG_ERROR("Loader", "Invalid NSO magic: 0x{:08X}", hdr->magic);
        return std::nullopt;
    }

    // Segment decompression or copy
    auto load_segment = [&](const NsoSegmentHeader& seg, u32 file_size, bool is_compressed) -> std::vector<u8> {
        std::vector<u8> out(seg.decompressed_size);
        if (seg.file_offset + file_size > data.size()) {
            NEMU_LOG_ERROR("Loader", "Segment out of bounds in NSO");
            return {};
        }

        auto seg_src = data.subspan(seg.file_offset, file_size);
        if (is_compressed) {
            if (!DecompressLZ4(seg_src, out)) {
                NEMU_LOG_ERROR("Loader", "Failed to LZ4-decompress NSO segment");
                return {};
            }
        } else {
            size_t copy_size = std::min<size_t>(file_size, seg.decompressed_size);
            std::memcpy(out.data(), seg_src.data(), copy_size);
        }
        return out;
    };

    const bool text_comp   = (hdr->flags & 1) != 0;
    const bool rodata_comp = (hdr->flags & 2) != 0;
    const bool data_comp   = (hdr->flags & 4) != 0;

    auto text_bytes   = load_segment(hdr->text, hdr->text_file_size, text_comp);
    auto rodata_bytes = load_segment(hdr->rodata, hdr->rodata_file_size, rodata_comp);
    auto data_bytes   = load_segment(hdr->data, hdr->data_file_size, data_comp);

    if (text_bytes.empty() || rodata_bytes.empty() || data_bytes.empty()) {
        NEMU_LOG_ERROR("Loader", "Failed to extract NSO segments");
        return std::nullopt;
    }

    u64 total_span = std::max({
        static_cast<u64>(hdr->text.memory_offset + hdr->text.decompressed_size),
        static_cast<u64>(hdr->rodata.memory_offset + hdr->rodata.decompressed_size),
        static_cast<u64>(hdr->data.memory_offset + hdr->data.decompressed_size + hdr->bss_size)
    });

    constexpr size_t PAGE_SIZE = 0x1000;
    u64 aligned_size = (total_span + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Build a flat module image (text + rodata + data at their memory_offsets).
    // Relocation tables are addressed by module-relative offsets that can land
    // in text, rodata, or data, so the flat image is required for parsing them
    // and is also what we write to guest memory.
    std::vector<u8> mapped_image(static_cast<size_t>(aligned_size), 0);
    if (hdr->text.memory_offset + text_bytes.size() <= mapped_image.size()) {
        std::memcpy(mapped_image.data() + hdr->text.memory_offset, text_bytes.data(), text_bytes.size());
    }
    if (hdr->rodata.memory_offset + rodata_bytes.size() <= mapped_image.size()) {
        std::memcpy(mapped_image.data() + hdr->rodata.memory_offset, rodata_bytes.data(), rodata_bytes.size());
    }
    if (hdr->data.memory_offset + data_bytes.size() <= mapped_image.size()) {
        std::memcpy(mapped_image.data() + hdr->data.memory_offset, data_bytes.data(), data_bytes.size());
    }

    // Apply Atmosphere IPS / IPS32 patches if available
    if (vfs != nullptr && title_id != 0) {
        PatchManager pm(*vfs);
        if (pm.ApplyExeFsPatches(title_id, hdr->module_id, mapped_image)) {
            NEMU_LOG_INFO("Loader", "Successfully applied Atmosphere IPS patches to NSO module");
        }
    }

    if (!vm.Map(base_address, aligned_size, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map NSO virtual memory at 0x{:016X}", base_address);
        return std::nullopt;
    }

    // Write the flat module image (text + rodata + data + zeroed BSS).
    vm.WriteBlock(base_address, mapped_image.data(), mapped_image.size());
    if (hdr->bss_size > 0) {
        std::vector<u8> zero_bss(hdr->bss_size, 0);
        vm.WriteBlock(base_address + hdr->data.memory_offset + hdr->data.decompressed_size,
                      zero_bss.data(), zero_bss.size());
    }

    // Apply ELF dynamic relocations (module-relative: RELATIVE / ABS64 / GLOB_DAT / JUMP_SLOT).
    if (leave_relocations) {
        NEMU_LOG_INFO("Loader", "Leaving relocations unapplied for module at 0x{:016X} (rtld self-relocates)", base_address);
    } else {
        ApplyRelocations(vm, base_address, mapped_image, hdr);
    }

    // Prime the module's self-referential GOT slots. Some GOT entries carry a
    // module-relative function address with NO relocation record against them
    // (verified on Terraria's rtld: slots +0x3170/+0x3178 hold raw 0x19b0) --
    // on a real console the kernel/loader fills these at map time, and rtld's
    // PLT-style thunks `ldr x17,[GOT]; br x17` otherwise branch to the raw
    // module-relative offset as an ABSOLUTE address (near-zero -> garbage
    // execution inside rtld's own init walk). Fill any unrelocated slot whose
    // raw value lies within the module image.
    PrimeUnrelocatedGotSlots(vm, base_address, mapped_image);

    // Apply permissions. When booting through rtld, leave EVERYTHING writable:
// rtld applies relocation writes across text/rodata GOT-adjacent regions
// (.data.rel.ro jump tables are written by rtld at boot, then made RO by the
// real kernel) and re-protects the modules itself. Reprotecting here made
// rtld's own tag-dispatch jump-table write fault (verified: store to
// 0x71002758, a jump-table entry address).
    if (leave_relocations) {
        NEMU_LOG_INFO("Loader", "Leaving module at 0x{:016X} fully writable (rtld re-protects after relocating)", base_address);
    } else {
        vm.Reprotect(base_address + hdr->text.memory_offset,
                     (hdr->text.decompressed_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                     memory::MemoryPermission::ReadExecute);

        vm.Reprotect(base_address + hdr->rodata.memory_offset,
                     (hdr->rodata.decompressed_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                     memory::MemoryPermission::Read);

        vm.Reprotect(base_address + hdr->data.memory_offset,
                     ((hdr->data.decompressed_size + hdr->bss_size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                     memory::MemoryPermission::ReadWrite);
    }

    NEMU_LOG_INFO("Loader", "NSO loaded successfully at 0x{:016X}, total size: 0x{:X}",
                  base_address, aligned_size);

    // Entry-point resolution. Commercial NSOs (Nintendo SDK) embed a module
    // header at the very start of .text:
    //   +0x00  u32  0 (padding / offset to the header below)
    //   +0x04  u32  offset from text start to a 'MOD0' struct
    //   +0x08  'MOD0' { dynamic, bss_start/end, eh_frame_hdr_start/end } + ext
    // Real code (_start / crt0) begins after that header; for Terraria's main
    // it is at +0x30 (SUB SP,SP,#0x90; STP...). Homebrew and rtld put real code
    // at +0 directly (rtld begins with a B instruction), so the skip must be
    // conditional. Scan for the first plausible ARM64 stack prologue pair after
    // the MOD0 header; fall back to text start.
    u64 entry_rel = 0;
    {
        u32 first_word = 0;
        u32 mod0_off = 0;
        if (text_bytes.size() >= 0x28) {
            std::memcpy(&first_word, text_bytes.data(), 4);
            std::memcpy(&mod0_off, text_bytes.data() + 4, 4);
        }
        const bool has_module_header =
            first_word == 0 && mod0_off >= 8 && mod0_off + 0x18 <= text_bytes.size() &&
            [&] {
                u32 m = 0;
                std::memcpy(&m, text_bytes.data() + mod0_off, 4);
                return m == 0x30444F4D; // 'MOD0'
            }();
        if (has_module_header) {
            constexpr size_t kScanLimit = 0x400;
            const size_t end = std::min(text_bytes.size() - 4, kScanLimit);
            for (size_t i = mod0_off + 0x18; i <= end; i += 4) {
                u32 w0 = 0, w1 = 0;
                std::memcpy(&w0, text_bytes.data() + i, 4);
                std::memcpy(&w1, text_bytes.data() + i + 4, 4);
                const bool w0_stack =
                    ((w0 & 0xFF80001F) == 0xD100001F && ((w0 >> 5) & 0x1F) == 0x1F) || // SUB/ADD SP,SP,#imm
                    ((w0 & 0x7E000000) == 0x28000000) ||                               // STP/LDP family
                    ((w0 & 0xFC000000) == 0x14000000);                                 // B
                const bool w1_stack =
                    ((w1 & 0xFF80001F) == 0xD100001F && ((w1 >> 5) & 0x1F) == 0x1F) ||
                    ((w1 & 0x7E000000) == 0x28000000) ||                               // STP/LDP family
                    ((w1 & 0xFC000000) == 0x94000000);                                 // BL
                if (w0_stack && w1_stack) {
                    entry_rel = i;
                    break;
                }
            }
            // If no stack prologue was found within the scan window, the code
            // still begins after the 0x100-byte embedded NSO/MOD0 module header
            // (hactool's text align_or_total_size). Fall back to that known
            // offset so we don't boot into the header padding.
            if (entry_rel == 0) {
                entry_rel = 0x100;
            }
            NEMU_LOG_INFO("Loader", "NSO module header (MOD0@+{:#x}); entry at text+{:#x}",
                          mod0_off, entry_rel);
        }
    }
    // Collect exported dynamic symbols (for cross-module symbol resolution).
    std::unordered_map<std::string, u64> exports;
    NsoLoader::CollectExportedSymbols(mapped_image, exports);

    // Synthesize the linker-script marker symbols rtld imports
    // (__rela_dyn_start/end, __rela_plt_start/end, __rel_dyn_*, __rel_plt_*,
    // __got_start/end, ...). These are NOT defined in any module's .dynsym --
    // on a real title the kernel/rtld supplies them from each NSO's own segment
    // layout, which is why rtld's imports mostly went unresolved under NEMU
    // (only 7/19 resolved) and why booting rtld relocated itself to a garbage
    // base. We can compute every derivable one from the module's .dynamic.
    NsoLoader::SynthesizeLinkerMarkers(mapped_image, base_address, aligned_size, exports);

    return NsoLoadedImage{
        .base_address = base_address,
        .entry_point = base_address + hdr->text.memory_offset + entry_rel,
        .total_size = aligned_size,
        .exported_symbols = std::move(exports),
        .image = std::move(mapped_image)
    };
}

size_t NsoLoader::CollectExportedSymbols(
    std::span<const u8> module_image,
    std::unordered_map<std::string, u64>& out
) {
    // Parse the module's .dynamic for DT_SYMTAB / DT_STRTAB / DT_SYMENT, then
    // record every *defined* (shndx != 0) symbol as name -> module-relative
    // value, so other modules can resolve their imports against it.
    if (module_image.size() < 0x20) return 0;

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= module_image.size(); i += 4) {
        u32 m = 0; std::memcpy(&m, module_image.data() + i, 4);
        if (m == 0x30444F4D) { mod0_offset = i; break; }
    }
    if (mod0_offset == std::string_view::npos) return 0;

    s32 dyn_rel = 0;
    std::memcpy(&dyn_rel, module_image.data() + mod0_offset + 4, sizeof(s32));
    const size_t dyn_off = mod0_offset + static_cast<size_t>(dyn_rel);

    u64 symtab = 0, strtab = 0, syment = 24, hash_tab = 0;
    size_t p = dyn_off;
    while (p + 16 <= module_image.size()) {
        s64 t = 0; u64 v = 0;
        std::memcpy(&t, module_image.data() + p, 8);
        std::memcpy(&v, module_image.data() + p + 8, 8);
        p += 16;
        if (t == 0) break;
        else if (t == 6) symtab = v;       // DT_SYMTAB
        else if (t == 5) strtab = v;       // DT_STRTAB
        else if (t == 11) syment = v;      // DT_SYMENT
        else if (t == 4) hash_tab = v;     // DT_HASH
    }
    if (symtab == 0 || strtab == 0 || syment < 24) return 0;

    // Bound the .dynsym walk to the *real* table extent.
    //
    // .dynsym has no DT_SYMENT-counted terminator: it runs straight into
    // .dynstr (DT_STRTAB), and everything past that is ordinary .rodata/.data
    // that merely looks like symbol records. Walking to the end of the flat
    // module image therefore invents tens of thousands of phantom symbols --
    // for Terraria's `main` that was 3,604,324 candidate entries against a real
    // table of 801, i.e. 4500x over-read. Because duplicate names are kept
    // first-wins, the phantom copies *shadowed* the genuine definitions: the
    // C++ allocator import `_Znwm` (operator new) resolved to the garbage
    // address 0x27471010755 instead of sdk's real 0x79A026B8. The game then
    // did `bl <thunk> -> ldr x17,[got]; br x17` into address 0, whose `RET`
    // popped a zero X30 and branched to PC 0 -- the "Low-branch ... -> next
    // 0x0" crash that stalled Terraria's init chain.
    //
    // DT_HASH is an ELF SysV hash table whose `nchain` field is, by definition,
    // the number of .dynsym entries. Prefer it; otherwise fall back to the
    // strtab, and only as a last resort to the image end.
    size_t sym_count = 0;
    if (hash_tab != 0 && hash_tab + 8 <= module_image.size()) {
        u32 nbucket = 0, nchain = 0;
        std::memcpy(&nbucket, module_image.data() + hash_tab, 4);
        std::memcpy(&nchain, module_image.data() + hash_tab + 4, 4);
        if (nchain > 0 && nbucket > 0) {
            sym_count = nchain;
        }
    }
    if (sym_count == 0 && strtab > symtab) {
        sym_count = (strtab - symtab) / syment;
    }
    if (sym_count == 0) {
        sym_count = (module_image.size() - symtab) / syment;
    }
    if (sym_count * syment > module_image.size() - std::min(symtab, module_image.size())) {
        sym_count = (module_image.size() - symtab) / syment;
    }

    size_t count = 0;
    for (size_t i = 0; i < sym_count; ++i) {
        const size_t off = symtab + i * syment;
        if (off + 24 > module_image.size()) break;
        u32 st_name = 0; u8 st_info = 0; u16 st_shndx = 0; u64 st_value = 0;
        std::memcpy(&st_name, module_image.data() + off, 4);
        st_info = module_image[off + 4];
        std::memcpy(&st_shndx, module_image.data() + off + 6, 2);
        std::memcpy(&st_value, module_image.data() + off + 8, 8);
        if (st_shndx == 0) continue;             // undefined / imported
        if (st_name == 0 || strtab + st_name >= module_image.size()) continue;
        // Bounds: name null-terminated within image.
        const u8* sp = module_image.data() + strtab + st_name;
        const u8* se = static_cast<const u8*>(std::memchr(sp, 0, module_image.size() - (strtab + st_name)));
        if (!se) continue;
        std::string name(reinterpret_cast<const char*>(sp), se - sp);
        if (name.empty()) continue;
        out.emplace(std::move(name), st_value);
        ++count;
    }
    return count;
}

void NsoLoader::PrimeUnrelocatedGotSlots(
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    std::span<const u8> module_image
) {
    // Fill GOT slots that (a) have no relocation record against them and
    // (b) hold a raw module-relative value within the module image. See the
    // call site for why (rtld's kernel-filled self-GOT convention).
    if (module_image.size() < 0x20) return;

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= module_image.size(); i += 4) {
        u32 m = 0; std::memcpy(&m, module_image.data() + i, 4);
        if (m == 0x30444F4D) { mod0_offset = i; break; }
    }
    if (mod0_offset == std::string_view::npos) return;

    s32 dyn_rel = 0;
    std::memcpy(&dyn_rel, module_image.data() + mod0_offset + 4, sizeof(s32));
    const size_t dyn_off = mod0_offset + static_cast<size_t>(dyn_rel);

    u64 rela_off = 0, rela_sz = 0, rela_ent = 24;
    u64 jmprel_off = 0, pltrel_sz = 0, pltrel_ent = 24;
    u64 pltgot = 0;
    size_t p = dyn_off;
    while (p + 16 <= module_image.size()) {
        s64 t = 0; u64 v = 0;
        std::memcpy(&t, module_image.data() + p, 8);
        std::memcpy(&v, module_image.data() + p + 8, 8);
        p += 16;
        if (t == 0) break;
        else if (t == 7)  rela_off = v;
        else if (t == 8)  rela_sz = v;
        else if (t == 9)  rela_ent = v ? v : 24;
        else if (t == 23) jmprel_off = v;
        else if (t == 2)  pltrel_sz = v;
        else if (t == 20) pltrel_ent = (v == 7) ? 24 : 16;
        else if (t == 3)  pltgot = v;
    }
    if (!pltgot || pltgot >= module_image.size()) {
        NEMU_LOG_DEBUG("Loader", "GotPrime: module at 0x{:016X} no usable DT_PLTGOT ({:#x})", base_address, pltgot);
        return;
    }

    std::unordered_set<u64> relocd;
    auto collect = [&](u64 off, u64 sz, u64 ent) {
        for (u64 e = off; e + 24 <= off + sz && e + 24 <= module_image.size(); e += ent) {
            u64 ro = 0;
            std::memcpy(&ro, module_image.data() + e, 8);
            relocd.insert(ro);
        }
    };
    collect(rela_off, rela_sz, rela_ent);
    // REL (non-a) entries are 16 bytes.
    for (u64 e = jmprel_off; e + 16 <= jmprel_off + pltrel_sz && e + 16 <= module_image.size(); e += pltrel_ent) {
        u64 ro = 0;
        std::memcpy(&ro, module_image.data() + e, 8);
        relocd.insert(ro);
    }

    size_t primed = 0;
    for (u64 off = pltgot & ~u64(7); off + 8 <= module_image.size(); off += 8) {
        if (relocd.count(off)) continue;
        u64 raw = 0;
        std::memcpy(&raw, module_image.data() + off, 8);
        if (raw == 0 || raw >= module_image.size()) continue;
        vm.Write64(base_address + off, base_address + raw);
        ++primed;
    }
    if (primed > 0) {
        NEMU_LOG_INFO("Loader", "Primed {} self-referential GOT slots for module at 0x{:016X}", primed, base_address);
    }
}

void NsoLoader::SynthesizeLinkerMarkers(
    std::span<const u8> module_image,
    vaddr_t base_address,
    size_t total_size,
    std::unordered_map<std::string, u64>& out
) {
    // rtld imports a family of linker-script marker symbols that no module
    // defines in .dynsym: the kernel hands rtld each NSO's marker values from
    // the module's own dynamic/segment layout. Without them rtld cannot walk
    // the modules' relocation tables (only 7/19 of its imports resolved for
    // Terraria), so booting rtld self-relocates to garbage. Compute what the
    // .dynamic can derive and register the rest with honest best-effort values.
    if (module_image.size() < 0x20) return;

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= module_image.size(); i += 4) {
        u32 m = 0; std::memcpy(&m, module_image.data() + i, 4);
        if (m == 0x30444F4D) { mod0_offset = i; break; }
    }
    if (mod0_offset == std::string_view::npos) return;

    s32 dyn_rel = 0, bss_start = 0, bss_end = 0;
    s32 eh_start = 0, eh_end = 0;
    std::memcpy(&dyn_rel, module_image.data() + mod0_offset + 4, sizeof(s32));
    std::memcpy(&bss_start, module_image.data() + mod0_offset + 8, sizeof(s32));
    std::memcpy(&bss_end, module_image.data() + mod0_offset + 12, sizeof(s32));
    std::memcpy(&eh_start, module_image.data() + mod0_offset + 16, sizeof(s32));
    std::memcpy(&eh_end, module_image.data() + mod0_offset + 20, sizeof(s32));
    const size_t dyn_off = mod0_offset + static_cast<size_t>(dyn_rel);

    u64 rela_off = 0, rela_sz = 0, rela_ent = 24;
    u64 jmprel_off = 0, pltrel_sz = 0, pltrel_ent = 24;
    u64 pltgot = 0;
    size_t p = dyn_off;
    while (p + 16 <= module_image.size()) {
        s64 t = 0; u64 v = 0;
        std::memcpy(&t, module_image.data() + p, 8);
        std::memcpy(&v, module_image.data() + p + 8, 8);
        p += 16;
        if (t == 0) break;
        else if (t == 7)  rela_off = v;      // DT_RELA
        else if (t == 8)  rela_sz = v;       // DT_RELASZ
        else if (t == 9)  rela_ent = v ? v : 24;
        else if (t == 23) jmprel_off = v;    // DT_JMPREL
        else if (t == 2)  pltrel_sz = v;     // DT_PLTRELSZ
        else if (t == 20) pltrel_ent = (v == 7) ? 24 : 16; // DT_PLTREL
        else if (t == 3)  pltgot = v;        // DT_PLTGOT
    }

    auto add = [&](const char* name, u64 module_rel_value) {
        // Markers are stored MODULE-RELATIVE like every other exported symbol
        // (the global link pass adds base_address). Storing absolute addresses
        // here double-bases them (rtld's __rela_dyn_start became 0xE2002010 =
        // 0x71000000 + 0x71002010) and rtld self-relocated to garbage.
        out.emplace(name, module_rel_value);
    };

    if (rela_off) {
        add("__rela_dyn_start", rela_off);
        add("__rela_dyn_end",   rela_off + rela_sz);
    }
    if (jmprel_off) {
        add("__rela_plt_start", jmprel_off);
        add("__rela_plt_end",   jmprel_off + pltrel_sz);
    }
    // Legacy non-a names used by some SDK builds.
    add("__rel_dyn_start",  rela_off);
    add("__rel_dyn_end",    rela_off + rela_sz);
    add("__rel_plt_start",  jmprel_off);
    add("__rel_plt_end",    jmprel_off + pltrel_sz);
    if (pltgot) {
        add("__got_start", pltgot);
        // __got_end: rtld's own table used the image end (RELA addend 0x4000 on
        // a 0x4000 module). Use the image end as the honest approximation.
        add("__got_end", total_size);
    }
    // __EX_start/end = eh_frame_hdr range; MOD0 +0x10/+0x14 carry it as
    // MOD0-relative offsets (this is what the real linker script exports).
    // When the MOD0 fields are 0 (small modules like rtld), fall back to the
    // whole-image extent: the markers must be NON-ZERO and inside the module,
    // because rtld reads them unconditionally and a 0 marker both fails its
    // own sanity walks and leaves its GOT import slot unresolved (verified on
    // the XCI rtld: __EX_*/TLS GLOB_DAT slots stayed 0 and its init walk then
    // double-based an address into 0xE200273C).
    add("__EX_start", mod0_offset + static_cast<size_t>(eh_start));
    add("__EX_end",   mod0_offset + static_cast<size_t>(eh_end ? eh_end : (total_size - mod0_offset)));
    // TLS markers: the SDK's __tdata_align_* / __tbss_align_* pairs. Without a
    // PT_TLS segment these should describe the (empty) TLS area at the end of
    // data — the bss region — never 0.
    const u64 bss_abs_start = mod0_offset + static_cast<size_t>(bss_start);
    const u64 bss_abs_end   = mod0_offset + static_cast<size_t>(bss_end ? bss_end : (total_size - mod0_offset));
    add("__tdata_align_abs", bss_abs_start);
    add("__tdata_align_rel", static_cast<u64>(bss_start));
    add("__tbss_align_abs",  bss_abs_end);
    add("__tbss_align_rel",  static_cast<u64>(bss_end));
    // NOTE: __nnDetailNintendoSdkRuntimeObjectFile is genuinely defined in
    // main's .dynsym and resolves normally; nothing to synthesize for it.
}

size_t NsoLoader::ApplyRelocations(
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    std::span<const u8> module_image,
    [[maybe_unused]] const NsoHeader* hdr
) {
    // Apply ELF dynamic relocations against a *flat* module image where text,
    // rodata, and data lie at their memory_offsets. The MOD0 header (usually
    // at flat offset ~0x8 in text) carries, at +4, the module-relative offset
    // of the .dynamic tag table, which for commercial games lands in rodata or
    // data. All addresses here are module-relative offsets into module_image.
    if (module_image.size() < 0x20) {
        return 0;
    }

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= module_image.size(); i += 4) {
        u32 magic = 0;
        std::memcpy(&magic, module_image.data() + i, sizeof(u32));
        if (magic == 0x30444F4D) { // 'MOD0'
            mod0_offset = i;
            break;
        }
    }
    if (mod0_offset == std::string_view::npos || mod0_offset + 0x1C > module_image.size()) {
        return 0;
    }

    // MOD0+4 = offset-from-MOD0 to the .dynamic tag table.
    s32 dynamic_from_mod0 = 0;
    std::memcpy(&dynamic_from_mod0, module_image.data() + mod0_offset + 4, sizeof(s32));
    const size_t dynamic_offset = mod0_offset + static_cast<size_t>(dynamic_from_mod0);
    if (dynamic_offset + 16 > module_image.size()) {
        return 0;
    }

    u64 rela_offset = 0;
    u64 rela_size = 0;
    u64 rela_ent = 24;

    size_t dyn_ptr = dynamic_offset;
    while (dyn_ptr + 16 <= module_image.size()) {
        s64 d_tag = 0;
        u64 d_val = 0;
        std::memcpy(&d_tag, module_image.data() + dyn_ptr, sizeof(s64));
        std::memcpy(&d_val, module_image.data() + dyn_ptr + 8, sizeof(u64));
        dyn_ptr += 16;

        if (d_tag == 0) { // DT_NULL
            break;
        } else if (d_tag == 7) { // DT_RELA
            rela_offset = d_val;
        } else if (d_tag == 8) { // DT_RELASZ
            rela_size = d_val;
        } else if (d_tag == 9) { // DT_RELAENT
            rela_ent = (d_val > 0) ? d_val : 24;
        }
    }

    if (rela_offset == 0 || rela_size == 0) {
        return 0;
    }

    size_t reloc_count = 0;
    const size_t num_relas = rela_size / rela_ent;
    for (size_t i = 0; i < num_relas; ++i) {
        const size_t entry = rela_offset + i * rela_ent;
        if (entry + rela_ent > module_image.size()) break;

        u64 r_offset = 0, r_info = 0, r_addend = 0;
        std::memcpy(&r_offset, module_image.data() + entry, 8);
        std::memcpy(&r_info,   module_image.data() + entry + 8, 8);
        std::memcpy(&r_addend, module_image.data() + entry + 16, 8);

        const u32 type = static_cast<u32>(r_info & 0xFFFFFFFF);
        const u64 sym_idx = r_info >> 32;

        // RELATIVE and ABS64 both store a base-relative location: the value
        // written is (module_base + addend).
        if (type == 1027 || type == 257) {
            vaddr_t patch_va = base_address + r_offset;
            if (vm.IsValidAddress(patch_va, 8)) {
                vm.Write64(patch_va, base_address + r_addend);
                ++reloc_count;
            }
        } else if (type == 1025 || type == 1026) { // GLOB_DAT / JUMP_SLOT
            if (sym_idx == 0) {
                // STN_UNDEF; best-effort intra-module data slot.
                vaddr_t patch_va = base_address + r_offset;
                if (vm.IsValidAddress(patch_va, 8)) {
                    vm.Write64(patch_va, base_address + r_addend);
                    ++reloc_count;
                }
            }
            // else: cross-module imported symbol -> left for a future
            // dynamic linker / lazy resolution; not resolved here.
        }
        // Other types are ignored.
    }

    if (reloc_count > 0) {
        NEMU_LOG_INFO("Loader", "Applied {} relocations to module at 0x{:016X}", reloc_count, base_address);
    }
    return reloc_count;
}

std::optional<NsoLoadedImage> NsoLoader::LoadFromFile(
    const std::string& host_path,
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    filesystem::VirtualFileSystem* vfs,
    u64 title_id
) {
    std::ifstream file(host_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NEMU_LOG_ERROR("Loader", "Could not open NSO file: {}", host_path);
        return std::nullopt;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        return std::nullopt;
    }

    std::vector<u8> buffer(static_cast<size_t>(file_size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    return Load(buffer, vm, base_address, vfs, title_id);
}

size_t NsoLoader::ResolveSymbolImports(
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    std::span<const u8> module_image,
    const std::unordered_map<std::string, u64>& global_symbols
) {
    // Walk the module's GLOB_DAT / JUMP_SLOT relocations with a non-undefined
    // symbol index, look the symbol up in the global (all-modules) export map,
    // and write its resolved guest address into the GOT slot.
    if (module_image.size() < 0x20 || global_symbols.empty()) return 0;

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= module_image.size(); i += 4) {
        u32 m = 0; std::memcpy(&m, module_image.data() + i, 4);
        if (m == 0x30444F4D) { mod0_offset = i; break; }
    }
    if (mod0_offset == std::string_view::npos) return 0;

    s32 dyn_rel = 0;
    std::memcpy(&dyn_rel, module_image.data() + mod0_offset + 4, sizeof(s32));
    const size_t dyn_off = mod0_offset + static_cast<size_t>(dyn_rel);

    u64 rela_offset = 0, rela_size = 0, rela_ent = 24;
    u64 jmprel_offset = 0, pltrels_size = 0, pltrel = 24;
    u64 symtab = 0, strtab = 0, syment = 24;
    size_t p = dyn_off;
    while (p + 16 <= module_image.size()) {
        s64 t = 0; u64 v = 0;
        std::memcpy(&t, module_image.data() + p, 8);
        std::memcpy(&v, module_image.data() + p + 8, 8);
        p += 16;
        if (t == 0) break;
        else if (t == 7) rela_offset = v;               // DT_RELA
        else if (t == 8) rela_size = v;                 // DT_RELASZ
        else if (t == 9) rela_ent = (v > 0) ? v : 24;   // DT_RELAENT
        else if (t == 23) jmprel_offset = v;            // DT_JMPREL (PLT jump-slots)
        else if (t == 2) pltrels_size = v;              // DT_PLTRELSZ
        else if (t == 20) pltrel = (v == 7) ? 24 : 16;  // DT_PLTREL: 7=RELA,17=REL
        else if (t == 6) symtab = v;                    // DT_SYMTAB
        else if (t == 5) strtab = v;                    // DT_STRTAB
        else if (t == 11) syment = v;                   // DT_SYMENT
    }
    if (symtab == 0 || strtab == 0) return 0;
    pltrel = (pltrel == 24) ? 24 : 16;

    // Resolve a single ELF reloc (RELA or REL entry) whose slot is r_offset and
    // whose symbol index is sym_idx. Look it up in the global symbol map and
    // write the resolved guest address into the module's GOT slot.
    auto apply_slot = [&](u64 r_offset_u, u64 sym_idx_u) -> bool {
        if (sym_idx_u == 0) return false;
        const size_t so = symtab + sym_idx_u * syment;
        if (so + 24 > module_image.size()) return false;
        u32 st_name = 0;
        std::memcpy(&st_name, module_image.data() + so, 4);
        if (st_name == 0 || strtab + st_name >= module_image.size()) return false;
        const u8* sp = module_image.data() + strtab + st_name;
        const u8* se = static_cast<const u8*>(std::memchr(sp, 0, module_image.size() - (strtab + st_name)));
        if (!se) return false;
        std::string name(reinterpret_cast<const char*>(sp), se - sp);

        u64 guest_addr = 0;
        // The __rel_dyn_* / __rel_plt_* markers are linker-provided and must
        // point at this module's own relocation tables (not a defined symbol).
        if (name == "__rel_dyn_start") guest_addr = base_address + rela_offset;
        else if (name == "__rel_dyn_end") guest_addr = base_address + rela_offset + rela_size;
        else if (name == "__rel_plt_start") guest_addr = base_address + jmprel_offset;
        else if (name == "__rel_plt_end") guest_addr = base_address + jmprel_offset + pltrels_size;
        else {
            auto it = global_symbols.find(name);
            if (it == global_symbols.end()) return false;
            guest_addr = it->second;
        }
        vaddr_t slot = base_address + r_offset_u;
        if (vm.IsValidAddress(slot, 8)) {
            vm.Write64(slot, guest_addr);
            return true;
        }
        return false;
    };

    size_t resolved = 0;

    // Pass 1: DT_RELA GLOB_DAT / JUMP_SLOT relocations.
    if (rela_offset != 0 && rela_size != 0) {
        const size_t num = rela_size / rela_ent;
        for (size_t i = 0; i < num; ++i) {
            const size_t e = rela_offset + i * rela_ent;
            if (e + rela_ent > module_image.size()) break;
            u64 r_offset = 0, r_info = 0;
            std::memcpy(&r_offset, module_image.data() + e, 8);
            std::memcpy(&r_info, module_image.data() + e + 8, 8);
            const u32 type = static_cast<u32>(r_info & 0xFFFFFFFF);
            if (type != 1025 && type != 1026) continue; // GLOB_DAT / JUMP_SLOT
            if (apply_slot(r_offset, r_info >> 32)) ++resolved;
        }
    }

    // Pass 2: DT_JMPREL lazy-binding JUMP_SLOT table (108 entries in main).
    // These carry the actual imported function pointers (strdup, longjmp,
    // printf, ...) that `BR X17` calls through.
    if (jmprel_offset != 0 && pltrels_size != 0) {
        const size_t num = pltrels_size / pltrel;
        for (size_t i = 0; i < num; ++i) {
            const size_t e = jmprel_offset + i * pltrel;
            if (e + pltrel > module_image.size()) break;
            u64 r_offset = 0, r_info = 0;
            std::memcpy(&r_offset, module_image.data() + e, 8);
            std::memcpy(&r_info, module_image.data() + e + 8, 8);
            const u32 type = static_cast<u32>(r_info & 0xFFFFFFFF);
            if (type != 1026) continue; // JUMP_SLOT
            if (apply_slot(r_offset, r_info >> 32)) ++resolved;
        }
    }

    if (resolved > 0) {
        NEMU_LOG_INFO("Loader", "Resolved {} symbol imports for module at 0x{:016X}", resolved, base_address);
    }
    return resolved;
}

} // namespace nemu::core::loader
