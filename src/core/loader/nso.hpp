#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <array>

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
};

class NsoLoader {
public:
    static constexpr u32 NSO_MAGIC = 0x304F534E; // 'NSO0'

    /// Load and map an NSO0 binary into guest VirtualMemory
    static std::optional<NsoLoadedImage> Load(
        std::span<const u8> data,
        memory::VirtualMemory& vm,
        vaddr_t base_address = 0x0071000000ULL
    );

    /// Load from host filesystem
    static std::optional<NsoLoadedImage> LoadFromFile(
        const std::string& host_path,
        memory::VirtualMemory& vm,
        vaddr_t base_address = 0x0071000000ULL
    );

    /// Decompress standard LZ4 block
    static bool DecompressLZ4(std::span<const u8> src, std::span<u8> dst);

    /// Parse ELF dynamic section (MOD0 / DT_RELA) and apply R_AARCH64_RELATIVE base relocations
    static size_t ApplyRelocations(
        memory::VirtualMemory& vm,
        vaddr_t base_address,
        std::span<const u8> rodata_bytes,
        std::span<const u8> data_bytes
    );
};

} // namespace nemu::core::loader
