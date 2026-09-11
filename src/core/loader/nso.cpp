#include "nso.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>

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
    vaddr_t base_address
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

    if (!vm.Map(base_address, aligned_size, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map NSO virtual memory at 0x{:016X}", base_address);
        return std::nullopt;
    }

    // Write text segment
    vm.WriteBlock(base_address + hdr->text.memory_offset, text_bytes.data(), text_bytes.size());
    // Write rodata segment
    vm.WriteBlock(base_address + hdr->rodata.memory_offset, rodata_bytes.data(), rodata_bytes.size());
    // Write data segment
    vm.WriteBlock(base_address + hdr->data.memory_offset, data_bytes.data(), data_bytes.size());

    // Zero BSS segment
    if (hdr->bss_size > 0) {
        std::vector<u8> zero_bss(hdr->bss_size, 0);
        vm.WriteBlock(base_address + hdr->data.memory_offset + hdr->data.decompressed_size,
                      zero_bss.data(), zero_bss.size());
    }

    // Apply ELF dynamic relocations (R_AARCH64_RELATIVE)
    ApplyRelocations(vm, base_address, rodata_bytes, data_bytes);

    // Apply permissions
    vm.Reprotect(base_address + hdr->text.memory_offset,
                 (hdr->text.decompressed_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                 memory::MemoryPermission::ReadExecute);

    vm.Reprotect(base_address + hdr->rodata.memory_offset,
                 (hdr->rodata.decompressed_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                 memory::MemoryPermission::Read);

    vm.Reprotect(base_address + hdr->data.memory_offset,
                 ((hdr->data.decompressed_size + hdr->bss_size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1),
                 memory::MemoryPermission::ReadWrite);

    NEMU_LOG_INFO("Loader", "NSO loaded successfully at 0x{:016X}, total size: 0x{:X}",
                  base_address, aligned_size);

    return NsoLoadedImage{
        .base_address = base_address,
        .entry_point = base_address + hdr->text.memory_offset,
        .total_size = aligned_size
    };
}

size_t NsoLoader::ApplyRelocations(
    memory::VirtualMemory& vm,
    vaddr_t base_address,
    std::span<const u8> rodata_bytes,
    [[maybe_unused]] std::span<const u8> data_bytes
) {
    if (rodata_bytes.size() < 0x20) {
        return 0;
    }

    size_t mod0_offset = std::string_view::npos;
    for (size_t i = 0; i + 4 <= rodata_bytes.size(); i += 4) {
        u32 magic = 0;
        std::memcpy(&magic, rodata_bytes.data() + i, sizeof(u32));
        if (magic == 0x30444F4D) { // 'MOD0'
            mod0_offset = i;
            break;
        }
    }

    if (mod0_offset == std::string_view::npos || mod0_offset + 0x1C > rodata_bytes.size()) {
        return 0;
    }

    s32 dynamic_rel_offset = 0;
    std::memcpy(&dynamic_rel_offset, rodata_bytes.data() + mod0_offset + 4, sizeof(s32));
    size_t dynamic_offset = static_cast<size_t>(static_cast<s64>(mod0_offset) + dynamic_rel_offset);

    if (dynamic_offset + 16 > rodata_bytes.size()) {
        return 0;
    }

    u64 rela_offset = 0;
    u64 rela_size = 0;
    u64 rela_ent = 24;

    size_t dyn_ptr = dynamic_offset;
    while (dyn_ptr + 16 <= rodata_bytes.size()) {
        s64 d_tag = 0;
        u64 d_val = 0;
        std::memcpy(&d_tag, rodata_bytes.data() + dyn_ptr, sizeof(s64));
        std::memcpy(&d_val, rodata_bytes.data() + dyn_ptr + 8, sizeof(u64));
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
        vaddr_t entry_addr = base_address + rela_offset + i * rela_ent;
        if (!vm.IsValidAddress(entry_addr, rela_ent)) break;

        const u64 r_offset = vm.Read64(entry_addr);
        const u64 r_info = vm.Read64(entry_addr + 8);
        const u64 r_addend = vm.Read64(entry_addr + 16);

        const u32 type = static_cast<u32>(r_info & 0xFFFFFFFF);
        if (type == 1027) { // R_AARCH64_RELATIVE
            vaddr_t patch_va = base_address + r_offset;
            u64 patched_val = base_address + r_addend;
            if (vm.IsValidAddress(patch_va, 8)) {
                vm.Write64(patch_va, patched_val);
                ++reloc_count;
            }
        }
    }

    if (reloc_count > 0) {
        NEMU_LOG_DEBUG("Loader", "Applied {} R_AARCH64_RELATIVE relocations to module at 0x{:016X}",
                       reloc_count, base_address);
    }
    return reloc_count;
}

std::optional<NsoLoadedImage> NsoLoader::LoadFromFile(
    const std::string& host_path,
    memory::VirtualMemory& vm,
    vaddr_t base_address
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

    return Load(buffer, vm, base_address);
}

} // namespace nemu::core::loader
