#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <array>
#include <unordered_map>

namespace nemu::core::filesystem {
class VirtualFileSystem;
}

namespace nemu::core::loader {

#pragma pack(push, 1)
struct NsoSegmentHeader {
    u32 file_offset{0};
    u32 memory_offset{0};
    u32 decompressed_size{0};
};

struct NsoHeader {
    u32 magic{0};                       // 'NSO0' (0x304F534E)
    u32 version{0};
    u32 reserved{0};
    u32 flags{0};                       // bit 0: text compressed, bit 1: rodata compressed, bit 2: data compressed
    NsoSegmentHeader text;
    u32 module_name_offset{0};
    NsoSegmentHeader rodata;
    u32 module_name_size{0};
    NsoSegmentHeader data;
    u32 bss_size{0};
    std::array<u8, 0x20> module_id{};
    u32 text_file_size{0};
    u32 rodata_file_size{0};
    u32 data_file_size{0};
    std::array<u8, 0x1C> reserved2{};
    std::array<u8, 0x20> text_hash{};
    std::array<u8, 0x20> rodata_hash{};
    std::array<u8, 0x20> data_hash{};
};
#pragma pack(pop)

struct NsoLoadedImage {
    vaddr_t base_address{0};
    vaddr_t entry_point{0};
    size_t total_size{0};

    /// Exported dynamic symbols (name -> module-relative value) for
    /// cross-module import resolution. Populated during Load.
    std::unordered_map<std::string, u64> exported_symbols;

    /// The flat module image (text+rodata+data at memory_offsets). Retained so
    /// the caller can resolve cross-module symbol imports after all modules are
    /// placed.
    std::vector<u8> image;
};

class NsoLoader {
public:
    static constexpr u32 NSO_MAGIC = 0x304F534E; // 'NSO0'

    /// Load and map an NSO0 binary into guest VirtualMemory.
    /// `leave_relocations`: skip loader-side relocation application. Required
    /// for the rtld module when booting through rtld — rtld re-processes its
    /// own GOT relative-relocs and ADDS the module base again, so pre-applied
    /// slots end up double-based (verified: store to 0xE200273C = rtld jump
    /// table 0x7100273C + base again). rtld does its own relocation work.
    static std::optional<NsoLoadedImage> Load(
        std::span<const u8> data,
        memory::VirtualMemory& vm,
        vaddr_t base_address = 0x0071000000ULL,
        filesystem::VirtualFileSystem* vfs = nullptr,
        u64 title_id = 0,
        bool leave_relocations = false
    );

    /// Load from host filesystem
    static std::optional<NsoLoadedImage> LoadFromFile(
        const std::string& host_path,
        memory::VirtualMemory& vm,
        vaddr_t base_address = 0x0071000000ULL,
        filesystem::VirtualFileSystem* vfs = nullptr,
        u64 title_id = 0
    );

    /// Decompress standard LZ4 block
    static bool DecompressLZ4(std::span<const u8> src, std::span<u8> dst);

    /// Parse ELF dynamic section (MOD0 / DT_RELA) and apply module-relative
    /// relocations (RELATIVE / ABS64 / GLOB_DAT / JUMP_SLOT) against the flat
    /// module image (text+rodata+data at their memory_offsets).
    static size_t ApplyRelocations(
        memory::VirtualMemory& vm,
        vaddr_t base_address,
        std::span<const u8> module_image,
        const struct NsoHeader* hdr
    );

    /// Collect every *defined* dynamic symbol (name -> module-relative value)
    /// from a flat module image's .dynsym / .dynstr.
    static size_t CollectExportedSymbols(
        std::span<const u8> module_image,
        std::unordered_map<std::string, u64>& out
    );

    /// Synthesize the linker-script marker symbols rtld imports
    /// (__rela_dyn_start/end, __rela_plt_start/end, __rel_*, __got_start/end).
    /// These are not defined in any module's .dynsym; the real kernel derives
    /// them from each NSO's dynamic/segment layout. Without them rtld cannot
    /// self-relocate or walk other modules' relocation tables.
    static void SynthesizeLinkerMarkers(
        std::span<const u8> module_image,
        vaddr_t base_address,
        size_t total_size,
        std::unordered_map<std::string, u64>& out
    );

    /// Patch GLOB_DAT / JUMP_SLOT slots that reference imported symbols, using
    /// a global name -> guest-address map (built from every loaded module's
    /// exported_symbols). This resolves cross-module C++/libc imports (e.g.
    /// `strdup`, `stdout`, `longjmp`, vtable/string symbol references).
    static size_t ResolveSymbolImports(
        memory::VirtualMemory& vm,
        vaddr_t base_address,
        std::span<const u8> module_image,
        const std::unordered_map<std::string, u64>& global_symbols
    );
};

} // namespace nemu::core::loader
