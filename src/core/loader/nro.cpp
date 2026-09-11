#include "nro.hpp"
#include "platform/logger.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace nemu::core::loader {

namespace {

constexpr size_t PAGE_SIZE = memory::VirtualMemory::PAGE_SIZE;
constexpr size_t PAGE_MASK = memory::VirtualMemory::PAGE_MASK;

constexpr vaddr_t PageAlignDown(vaddr_t addr) noexcept {
    return addr & ~PAGE_MASK;
}

constexpr vaddr_t PageAlignUp(vaddr_t addr) noexcept {
    return (addr + PAGE_MASK) & ~PAGE_MASK;
}

} // namespace

bool NroLoader::IsValidNro(std::span<const u8> file_data) noexcept {
    if (file_data.size() < sizeof(NroHeader)) {
        return false;
    }

    const auto* header = reinterpret_cast<const NroHeader*>(file_data.data());
    if (header->magic != NRO_MAGIC) {
        return false;
    }

    if (header->size > file_data.size()) {
        return false;
    }

    // Verify text segment bounds
    if (header->text.file_offset + header->text.size > file_data.size()) {
        return false;
    }

    // Verify rodata segment bounds
    if (header->rodata.file_offset + header->rodata.size > file_data.size()) {
        return false;
    }

    // Verify data segment bounds
    if (header->data.file_offset + header->data.size > file_data.size()) {
        return false;
    }

    return true;
}

std::optional<LoadedNroInfo> NroLoader::Load(
    std::span<const u8> file_data,
    memory::VirtualMemory& memory,
    vaddr_t load_address) {

    if (!IsValidNro(file_data)) {
        NEMU_LOG_ERROR("Loader", "Invalid NRO binary supplied");
        return std::nullopt;
    }

    if ((load_address & PAGE_MASK) != 0) {
        NEMU_LOG_ERROR("Loader", "Unaligned load address 0x{:016X}", load_address);
        return std::nullopt;
    }

    const auto* header = reinterpret_cast<const NroHeader*>(file_data.data());

    // Compute segment virtual addresses and aligned sizes
    const vaddr_t text_vaddr = PageAlignDown(load_address + header->text.file_offset);
    const size_t text_vsize = PageAlignUp(load_address + header->text.file_offset + header->text.size) - text_vaddr;

    const vaddr_t rodata_vaddr = PageAlignDown(load_address + header->rodata.file_offset);
    const size_t rodata_vsize = PageAlignUp(load_address + header->rodata.file_offset + header->rodata.size) - rodata_vaddr;

    const vaddr_t data_vaddr = PageAlignDown(load_address + header->data.file_offset);
    const size_t data_vsize = PageAlignUp(load_address + header->data.file_offset + header->data.size) - data_vaddr;

    const vaddr_t bss_vaddr = data_vaddr + data_vsize;
    const size_t bss_vsize = PageAlignUp(header->bss_size);

    NEMU_LOG_INFO("Loader", "Loading NRO: text [0x{:016X}-0x{:016X}], rodata [0x{:016X}-0x{:016X}], data [0x{:016X}-0x{:016X}], bss [0x{:016X}-0x{:016X}]",
        text_vaddr, text_vaddr + text_vsize,
        rodata_vaddr, rodata_vaddr + rodata_vsize,
        data_vaddr, data_vaddr + data_vsize,
        bss_vaddr, bss_vaddr + bss_vsize);

    // Map segments initially as ReadWrite so we can write section data into memory
    if (!memory.Map(text_vaddr, text_vsize, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map text segment");
        return std::nullopt;
    }

    if (rodata_vsize > 0 && !memory.Map(rodata_vaddr, rodata_vsize, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map rodata segment");
        memory.Unmap(text_vaddr, text_vsize);
        return std::nullopt;
    }

    if (data_vsize > 0 && !memory.Map(data_vaddr, data_vsize, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map data segment");
        memory.Unmap(text_vaddr, text_vsize);
        if (rodata_vsize > 0) memory.Unmap(rodata_vaddr, rodata_vsize);
        return std::nullopt;
    }

    if (bss_vsize > 0 && !memory.Map(bss_vaddr, bss_vsize, memory::MemoryPermission::ReadWrite)) {
        NEMU_LOG_ERROR("Loader", "Failed to map bss segment");
        memory.Unmap(text_vaddr, text_vsize);
        if (rodata_vsize > 0) memory.Unmap(rodata_vaddr, rodata_vsize);
        if (data_vsize > 0) memory.Unmap(data_vaddr, data_vsize);
        return std::nullopt;
    }

    // Write text data
    if (header->text.size > 0) {
        if (!memory.WriteBlock(load_address + header->text.file_offset,
                               file_data.data() + header->text.file_offset,
                               header->text.size)) {
            NEMU_LOG_ERROR("Loader", "Failed to write text segment");
            return std::nullopt;
        }
    }

    // Write rodata data
    if (header->rodata.size > 0) {
        if (!memory.WriteBlock(load_address + header->rodata.file_offset,
                               file_data.data() + header->rodata.file_offset,
                               header->rodata.size)) {
            NEMU_LOG_ERROR("Loader", "Failed to write rodata segment");
            return std::nullopt;
        }
    }

    // Write data section
    if (header->data.size > 0) {
        if (!memory.WriteBlock(load_address + header->data.file_offset,
                               file_data.data() + header->data.file_offset,
                               header->data.size)) {
            NEMU_LOG_ERROR("Loader", "Failed to write data segment");
            return std::nullopt;
        }
    }

    // Reprotect text to ReadExecute
    if (!memory.Reprotect(text_vaddr, text_vsize, memory::MemoryPermission::ReadExecute)) {
        NEMU_LOG_ERROR("Loader", "Failed to reprotect text segment to ReadExecute");
        return std::nullopt;
    }

    // Reprotect rodata to Read
    if (rodata_vsize > 0 && !memory.Reprotect(rodata_vaddr, rodata_vsize, memory::MemoryPermission::Read)) {
        NEMU_LOG_ERROR("Loader", "Failed to reprotect rodata segment to Read");
        return std::nullopt;
    }

    LoadedNroInfo info{};
    info.load_address = load_address;
    info.total_mapped_size = text_vsize + rodata_vsize + data_vsize + bss_vsize;
    info.text_address = text_vaddr;
    info.text_size = text_vsize;
    info.rodata_address = rodata_vaddr;
    info.rodata_size = rodata_vsize;
    info.data_address = data_vaddr;
    info.data_size = data_vsize;
    info.bss_address = bss_vaddr;
    info.bss_size = bss_vsize;
    info.entry_point = load_address; // In NRO, entry point is at the start (instruction at offset 0)
    std::memcpy(info.build_id.data(), header->build_id, sizeof(header->build_id));

    NEMU_LOG_INFO("Loader", "Successfully loaded NRO at 0x{:016X}, entry point: 0x{:016X}, total size: 0x{:X}",
        info.load_address, info.entry_point, info.total_mapped_size);

    return info;
}

std::optional<LoadedNroInfo> NroLoader::LoadFromFile(
    std::string_view file_path,
    memory::VirtualMemory& memory,
    vaddr_t load_address) {

    std::ifstream stream(std::string(file_path), std::ios::binary | std::ios::ate);
    if (!stream.is_open()) {
        NEMU_LOG_ERROR("Loader", "Failed to open NRO file: {}", file_path);
        return std::nullopt;
    }

    const auto file_size = stream.tellg();
    if (file_size <= 0) {
        NEMU_LOG_ERROR("Loader", "Empty NRO file: {}", file_path);
        return std::nullopt;
    }

    stream.seekg(0, std::ios::beg);
    std::vector<u8> buffer(static_cast<size_t>(file_size));
    if (!stream.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        NEMU_LOG_ERROR("Loader", "Failed to read NRO file data: {}", file_path);
        return std::nullopt;
    }

    return Load(buffer, memory, load_address);
}

} // namespace nemu::core::loader
