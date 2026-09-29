#include "ncz.hpp"
#include "platform/logger.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>

#if __has_include(<zstd.h>)
#include <zstd.h>
#define NEMU_HAS_ZSTD 1
#else
#define NEMU_HAS_ZSTD 0
#endif

namespace nemu::core::loader {

#pragma pack(push, 1)
struct NczSectionHeader {
    u64 magic;          // 'NCZSECTN'
    u64 section_count;
};

struct NczSectionEntry {
    u64 offset;
    u64 size;
    u64 crypto_type;
    u64 padding;
};

struct NczBlockHeader {
    u64 magic;          // 'NCZBLOCK'
    u8 version;
    u8 type;
    u8 unused[2];
    u32 block_size_exponent;
    u64 total_size;
    u64 block_count;
};
#pragma pack(pop)

bool NczDecompressor::IsNcz(std::span<const u8> data) {
    if (data.size() < HEADER_SIZE + sizeof(NczSectionHeader)) {
        return false;
    }

    // Check NCZSECTN magic at offset 0x4000
    u64 magic = 0;
    std::memcpy(&magic, data.data() + HEADER_SIZE, sizeof(u64));
    if (magic == NCZSECTN_MAGIC) {
        return true;
    }

    // Check direct ZSTD magic at 0x4000
    u32 zstd_mag = 0;
    std::memcpy(&zstd_mag, data.data() + HEADER_SIZE, sizeof(u32));
    if (zstd_mag == ZSTD_MAGIC) {
        return true;
    }

    return false;
}

std::optional<std::vector<u8>> NczDecompressor::Decompress(std::span<const u8> data) {
    if (data.size() < HEADER_SIZE) {
        NEMU_LOG_ERROR("Loader", "NCZ data buffer too small");
        return std::nullopt;
    }

#if !NEMU_HAS_ZSTD
    NEMU_LOG_ERROR("Loader", "Zstandard decompression is not supported in this build configuration");
    return std::nullopt;
#else
    // Copy the uncompressed 16 KiB NCA header
    std::vector<u8> decompressed_nca;
    decompressed_nca.reserve(data.size() * 2);
    decompressed_nca.insert(decompressed_nca.end(), data.begin(), data.begin() + HEADER_SIZE);

    size_t cursor = HEADER_SIZE;
    u64 sectn_mag = 0;
    if (cursor + sizeof(u64) <= data.size()) {
        std::memcpy(&sectn_mag, data.data() + cursor, sizeof(u64));
    }

    if (sectn_mag == NCZSECTN_MAGIC) {
        NczSectionHeader sect_hdr{};
        std::memcpy(&sect_hdr, data.data() + cursor, sizeof(NczSectionHeader));
        cursor += sizeof(NczSectionHeader);

        const size_t entries_size = static_cast<size_t>(sect_hdr.section_count) * sizeof(NczSectionEntry);
        if (cursor + entries_size > data.size()) {
            NEMU_LOG_ERROR("Loader", "Corrupt NCZ: section entries exceed buffer size");
            return std::nullopt;
        }
        cursor += entries_size;
    }

    if (cursor >= data.size()) {
        NEMU_LOG_ERROR("Loader", "NCZ data truncated before payload");
        return std::nullopt;
    }

    // Check for NCZBLOCK header
    u64 block_mag = 0;
    if (cursor + sizeof(u64) <= data.size()) {
        std::memcpy(&block_mag, data.data() + cursor, sizeof(u64));
    }

    if (block_mag == NCZBLOCK_MAGIC) {
        if (cursor + sizeof(NczBlockHeader) > data.size()) {
            NEMU_LOG_ERROR("Loader", "Corrupt NCZ: truncated NCZBLOCK header");
            return std::nullopt;
        }

        NczBlockHeader blk_hdr{};
        std::memcpy(&blk_hdr, data.data() + cursor, sizeof(NczBlockHeader));
        cursor += sizeof(NczBlockHeader);

        const size_t block_size = 1ULL << blk_hdr.block_size_exponent;
        const size_t block_count = static_cast<size_t>(blk_hdr.block_count);
        const size_t table_size = block_count * sizeof(u32);

        if (cursor + table_size > data.size()) {
            NEMU_LOG_ERROR("Loader", "Corrupt NCZ: block size table exceeds buffer");
            return std::nullopt;
        }

        std::vector<u32> comp_sizes(block_count);
        std::memcpy(comp_sizes.data(), data.data() + cursor, table_size);
        cursor += table_size;

        decompressed_nca.resize(HEADER_SIZE + blk_hdr.total_size);
        u8* out_ptr = decompressed_nca.data() + HEADER_SIZE;
        size_t bytes_written = 0;

        for (size_t b = 0; b < block_count; ++b) {
            const u32 csize = comp_sizes[b];
            if (cursor + csize > data.size()) {
                NEMU_LOG_ERROR("Loader", "Corrupt NCZ: block {} exceeds buffer", b);
                return std::nullopt;
            }

            const size_t cur_block_out = std::min(block_size, static_cast<size_t>(blk_hdr.total_size - bytes_written));
            const size_t res = ZSTD_decompress(out_ptr + bytes_written, cur_block_out, data.data() + cursor, csize);
            if (ZSTD_isError(res)) {
                NEMU_LOG_ERROR("Loader", "ZSTD block decompression failed: {}", ZSTD_getErrorName(res));
                return std::nullopt;
            }

            bytes_written += res;
            cursor += csize;
        }

        NEMU_LOG_INFO("Loader", "Successfully decompressed NCZ block stream: {} bytes -> {} bytes",
                      data.size(), decompressed_nca.size());
        return decompressed_nca;
    }

    // Direct streaming ZSTD payload
    const u8* zstd_src = data.data() + cursor;
    const size_t zstd_src_size = data.size() - cursor;

    unsigned long long frame_size = ZSTD_getFrameContentSize(zstd_src, zstd_src_size);
    if (frame_size == ZSTD_CONTENTSIZE_ERROR) {
        NEMU_LOG_ERROR("Loader", "Invalid Zstandard compressed frame header");
        return std::nullopt;
    }

    if (frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Fallback: allocate dynamic decompression buffer
        frame_size = zstd_src_size * 4;
    }

    decompressed_nca.resize(HEADER_SIZE + frame_size);
    size_t decomp_res = ZSTD_decompress(decompressed_nca.data() + HEADER_SIZE, frame_size, zstd_src, zstd_src_size);
    if (ZSTD_isError(decomp_res)) {
        NEMU_LOG_ERROR("Loader", "ZSTD frame decompression failed: {}", ZSTD_getErrorName(decomp_res));
        return std::nullopt;
    }

    decompressed_nca.resize(HEADER_SIZE + decomp_res);
    NEMU_LOG_INFO("Loader", "Successfully decompressed NCZ streaming frame: {} bytes -> {} bytes",
                  data.size(), decompressed_nca.size());
    return decompressed_nca;
#endif
}

bool NczDecompressor::IsSplitVolume(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();
    if (ext.empty() || ext.size() < 2) return false;

    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // .xc0, .xc00, .ns0, .ns00, .00, .000
    if (ext.starts_with(".xc") && ext.size() >= 4 && ext[3] == '0') return true;
    if (ext.starts_with(".ns") && ext.size() >= 4 && ext[3] == '0') return true;
    if (ext == ".xc0" || ext == ".ns0" || ext == ".00" || ext == ".000") return true;

    return false;
}

std::string NczDecompressor::GetCanonicalExtension(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext.find("xc") != std::string::npos) return ".xci";
    if (ext.find("ns") != std::string::npos) return ".nsp";
    return ".xci";
}

std::vector<std::string> NczDecompressor::GetSplitParts(const std::string& first_part_path) {
    std::vector<std::string> parts;
    std::filesystem::path p(first_part_path);
    if (!std::filesystem::exists(p)) {
        return parts;
    }

    parts.push_back(p.string());

    std::string ext = p.extension().string();
    std::string stem = p.stem().string();
    auto dir = p.parent_path();

    // Check if format is .xc0, .xc1, ...
    if (ext == ".xc0" || ext == ".ns0" || ext == ".00") {
        std::string base_ext = ext.substr(0, ext.size() - 1); // e.g. ".xc", ".ns", ".0"
        for (int i = 1; i < 100; ++i) {
            std::string next_ext = base_ext + std::to_string(i);
            auto next_path = dir / (stem + next_ext);
            if (std::filesystem::exists(next_path)) {
                parts.push_back(next_path.string());
            } else {
                break;
            }
        }
    } else if (ext == ".000" || ext == ".xc00" || ext == ".ns00") {
        std::string base_ext = ext.substr(0, ext.size() - 2);
        for (int i = 1; i < 100; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d", i);
            auto next_path = dir / (stem + base_ext + buf);
            if (std::filesystem::exists(next_path)) {
                parts.push_back(next_path.string());
            } else {
                break;
            }
        }
    }

    return parts;
}

} // namespace nemu::core::loader
