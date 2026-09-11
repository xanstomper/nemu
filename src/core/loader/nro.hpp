#pragma once

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/cpu/cpu_state.hpp"
#include <string_view>
#include <span>
#include <vector>
#include <optional>

namespace nemu::core::loader {

#pragma pack(push, 1)
struct NroSegmentHeader {
    u32 file_offset;
    u32 size;
};

struct NroHeader {
    u32 entry_point_instruction;
    u32 mod0_offset;
    u8  padding[8];
    u32 magic; // 'NRO0' = 0x304F524E
    u32 version;
    u32 size;
    u32 flags;
    NroSegmentHeader text;
    NroSegmentHeader rodata;
    NroSegmentHeader data;
    u32 bss_size;
    u8  reserved[4];
    u8  build_id[32];
    u32 dso_handle_offset;
    u8  reserved2[4];
    NroSegmentHeader embedded_aset;
};
#pragma pack(pop)

static_assert(sizeof(NroHeader) == 0x70, "NroHeader size mismatch");
static_assert(sizeof(NroSegmentHeader) == 8, "NroSegmentHeader size mismatch");

struct LoadedNroInfo {
    vaddr_t load_address{0};
    size_t total_mapped_size{0};
    vaddr_t text_address{0};
    size_t text_size{0};
    vaddr_t rodata_address{0};
    size_t rodata_size{0};
    vaddr_t data_address{0};
    size_t data_size{0};
    vaddr_t bss_address{0};
    size_t bss_size{0};
    vaddr_t entry_point{0};
    std::array<u8, 32> build_id{};
};

class NroLoader {
public:
    static constexpr u32 NRO_MAGIC = 0x304F524E; // 'NRO0'
    static constexpr vaddr_t DEFAULT_NRO_LOAD_ADDRESS = 0x0071000000ULL;

    static bool IsValidNro(std::span<const u8> file_data) noexcept;

    static std::optional<LoadedNroInfo> Load(
        std::span<const u8> file_data,
        memory::VirtualMemory& memory,
        vaddr_t load_address = DEFAULT_NRO_LOAD_ADDRESS);

    static std::optional<LoadedNroInfo> LoadFromFile(
        std::string_view file_path,
        memory::VirtualMemory& memory,
        vaddr_t load_address = DEFAULT_NRO_LOAD_ADDRESS);
};

} // namespace nemu::core::loader
