// SPDX-FileCopyrightText: NEMU Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests for the ported NVDEC engine + nvhost-nvdec device ioctls
// (citron-derived port, commit-class: game-compat NVDEC phase 1).

#include "core/types.hpp"
#include "core/gpu/nvhost/nvhost_nvdec.hpp"
#include "core/gpu/nvhost/h264.hpp"
#include "core/gpu/nvhost/nvdevice.hpp"
#include "core/gpu/nvhost/nvmap.hpp"
#include "core/gpu/nvhost/nvhost_ctrl.hpp"
#include "core/memory/virtual_memory.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace nemu::core;
using namespace nemu::core::gpu::nvhost;
using nemu::u32;
using nemu::u64;
using nemu::u8;
using nemu::s32;
using nemu::vaddr_t;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] Assertion failed: " #cond " at %s:%d\n", __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

static void TestNvdecRegisterWrite() {
    memory::VirtualMemory vmem;
    Nvdec nvdec(&vmem);

    // set_codec_id (register slot 0x80): write H264 (0x3).
    nvdec.CallMethod(NvdecRegisters::kRegSetCodecId, 0x3);
    NEMU_TEST_ASSERT(nvdec.GetCodec() == VideoCodec::H264);

    nvdec.CallMethod(NvdecRegisters::kRegSetCodecId, 0x9);
    NEMU_TEST_ASSERT(nvdec.GetCodec() == VideoCodec::VP9);

    // execute triggers an attempt; unsupported-codec path must not crash.
    nvdec.CallMethod(NvdecRegisters::kRegExecute, 1);
    NEMU_TEST_ASSERT(nvdec.GetStats().execute_calls == 1);
    NEMU_TEST_ASSERT(nvdec.GetStats().decode_attempts == 1);

    // An invalid codec enum must be counted, not decoded.
    nvdec.CallMethod(NvdecRegisters::kRegSetCodecId, 0x99);
    nvdec.CallMethod(NvdecRegisters::kRegExecute, 1);
    NEMU_TEST_ASSERT(nvdec.GetStats().unsupported_codec_calls == 1);
    NEMU_TEST_ASSERT(nvdec.GetStats().decode_attempts == 1); // unchanged
    std::puts("  PASS TestNvdecRegisterWrite");
}

static void TestNvdecDeviceIoctls() {
    auto nvmap = std::make_shared<NvMap>();
    auto syncpoints = std::make_shared<SyncpointManager>();
    memory::VirtualMemory vmem;

    NvHostNvdecDevice dev(nvmap, syncpoints, &vmem);

    // SetNVMAPfd (group 'H', cmd 0x1): word = 0x4801 | (size<<16).
    {
        const u32 cmd = (0x4U << 16) | (0x1U << 8) | 'H';
        u32 fd_val = 7;
        u32 out = 0;
        const u32 res = dev.Ioctl(cmd,
            std::span<const u8>(reinterpret_cast<const u8*>(&fd_val), sizeof(fd_val)),
            std::span<u8>(reinterpret_cast<u8*>(&out), sizeof(out)));
        NEMU_TEST_ASSERT(res == 0);
    }

    // GetSyncpoint (group 0, cmd 0x2): returns the fixed channel syncpoint.
    {
        const u32 cmd = (0x8U << 16) | (0x2U << 8) | 0x0;
        u32 in = 0; u32 out = 0;
        const u32 res = dev.Ioctl(cmd,
            std::span<const u8>(reinterpret_cast<const u8*>(&in), sizeof(in)),
            std::span<u8>(reinterpret_cast<u8*>(&out), sizeof(out)));
        NEMU_TEST_ASSERT(res == 0);
        NEMU_TEST_ASSERT(out == 0x41);
    }

    // GetWaitbase (group 0, cmd 0x3): hard-coded 0.
    {
        const u32 cmd = (0x8U << 16) | (0x3U << 8) | 0x0;
        u32 in = 0; u32 out = 0xFF;
        const u32 res = dev.Ioctl(cmd,
            std::span<const u8>(reinterpret_cast<const u8*>(&in), sizeof(in)),
            std::span<u8>(reinterpret_cast<u8*>(&out), sizeof(out)));
        NEMU_TEST_ASSERT(res == 0);
        NEMU_TEST_ASSERT(out == 0);
    }

    // Unimplemented ioctls must return a bad-parameter NvResult, not crash.
    {
        const u32 cmd = (0x4U << 16) | (0xF0U << 8) | 0x0;
        u32 in = 0; u32 out = 0;
        const u32 res = dev.Ioctl(cmd,
            std::span<const u8>(reinterpret_cast<const u8*>(&in), sizeof(in)),
            std::span<u8>(reinterpret_cast<u8*>(&out), sizeof(out)));
        NEMU_TEST_ASSERT(res != 0);
    }
    std::puts("  PASS TestNvdecDeviceIoctls");
}

static void TestNvdecSubmitRouting() {
    auto nvmap = std::make_shared<NvMap>();
    auto syncpoints = std::make_shared<SyncpointManager>();
    memory::VirtualMemory vmem;

    // Guest memory backing: one page for the nvmap allocation, one for the
    // command buffer words.
    const vaddr_t buf_va = 0x00A0000000ULL;
    NEMU_TEST_ASSERT(vmem.Map(buf_va, memory::VirtualMemory::PAGE_SIZE,
                              memory::MemoryPermission::ReadWrite));

    // Create + allocate an nvmap handle for the command buffer.
    const u32 handle = nvmap->Create(0x1000);
    NEMU_TEST_ASSERT(handle != 0);
    NEMU_TEST_ASSERT(nvmap->Alloc(handle, 0, 0, 4096, 0, buf_va));

    NvHostNvdecDevice dev(nvmap, syncpoints, &vmem);

    // Command buffer: set_codec_id = H264, then execute.
    struct { u32 method; u32 arg; } words[2] = {
        {NvdecRegisters::kRegSetCodecId * 2, 0x3}, // byte-addressed convention
        {NvdecRegisters::kRegExecute * 2, 0x1},
    };
    const vaddr_t words_va = buf_va;
    vmem.WriteBlock(words_va, words, sizeof(words));

    // Submit header + one CommandBuffer pointing at the words.
    struct SubmitPacket {
        u32 cmd_buffer_count; u32 reloc_count; u32 syncpt_count; u32 fence_count;
        s32 memory_id; u32 offset; s32 word_count;
    } packet{ 1, 0, 0, 0, static_cast<s32>(handle), 0, 4 };
    // NOTE: word_count = 4 raw u32 words = {method,arg} pairs x2.

    const u32 cmd = (0x10U << 16) | (0x1U << 8) | 0x0;
    std::vector<u8> out(sizeof(packet), 0);
    const u32 res = dev.Ioctl(cmd,
        std::span<const u8>(reinterpret_cast<const u8*>(&packet), sizeof(packet)),
        std::span<u8>(out.data(), out.size()));
    NEMU_TEST_ASSERT(res == 0);

    // The submit must have routed to the engine: codec set + one decode attempt.
    NEMU_TEST_ASSERT(dev.GetEngine().GetCodec() == VideoCodec::H264);
    NEMU_TEST_ASSERT(dev.GetEngine().GetStats().execute_calls == 1);
    NEMU_TEST_ASSERT(dev.GetEngine().GetStats().decode_attempts == 1);

    // No frames can be produced yet (ffmpeg path absent) — PopFrame false.
    DecodedVideoFrame frame;
    NEMU_TEST_ASSERT(!dev.GetEngine().PopFrame(frame));
    std::puts("  PASS TestNvdecSubmitRouting");
}

static void TestH264Composer() {
    memory::VirtualMemory vmem;
    const vaddr_t mem_va = 0x00B0000000ULL;
    NEMU_TEST_ASSERT(vmem.Map(mem_va, memory::VirtualMemory::PAGE_SIZE * 4,
                              memory::MemoryPermission::ReadWrite));

    // Build a minimal H264DecoderContext at the picture_info offset.
    // Offsets per the HW struct: stream_len at +0x48, params at +0x58.
    const vaddr_t pic_info_va = mem_va;
    const vaddr_t bitstream_va = mem_va + 0x800;
    // Synthetic bitstream (16 bytes).
    for (int i = 0; i < 16; ++i) {
        u8 b = static_cast<u8>(0xA0 + i);
        vmem.WriteBlock(bitstream_va + static_cast<vaddr_t>(i), &b, 1);
    }
    // stream_len = 16.
    u32 stream_len = 16;
    vmem.WriteBlock(pic_info_va + 0x48, &stream_len, 4);
    // H264ParameterSet at +0x58: pick benign values. Width 2 MBs (352px? no,
    // 2 MBs = 32px), height 2 map units, frame_mbs_only=1, parameter_flags
    // with frame_number=0 (forces header path), chroma_format=1, poc_type=0.
    u32 width_mbs = 2, height_units = 2;
    vmem.WriteBlock(pic_info_va + 0x5C, &width_mbs, 4);   // +0x0C in PS
    vmem.WriteBlock(pic_info_va + 0x60, &height_units, 4);// +0x10 in PS
    u32 mbs_only = 1;
    vmem.WriteBlock(pic_info_va + 0x68, &mbs_only, 4);    // +0x08 in PS
    // parameter_flags at +0x58+0x58 = 0xB0: frame_number(46:16)=0,
    // chroma_format_idc(12:2)=1, pic_order_cnt_type(14:2)=0,
    // log2_max_frame_num_minus4(8:4)=0.
    u64 flags = (1ULL << 12); // chroma_format_idc = 1
    vmem.WriteBlock(pic_info_va + 0xB0, &flags, 8);

    NvdecRegisters regs{};
    regs.reg_array[NvdecRegisters::kRegPictureInfoOffset] = pic_info_va << 8;
    regs.reg_array[NvdecRegisters::kRegFrameBitstreamOffset] = bitstream_va << 8;

    decoder::H264 composer(&vmem);
    std::vector<u8> packet;
    size_t config_size = 0;
    const bool ok = composer.ComposeFrame(regs, packet, &config_size, /*is_first=*/true);
    NEMU_TEST_ASSERT(ok && "composer must succeed on well-formed registers");
    // Header must exist and start with the Annex-B start code prefix.
    NEMU_TEST_ASSERT(config_size >= 4);
    NEMU_TEST_ASSERT(packet.size() == config_size + 16);
    NEMU_TEST_ASSERT(packet[0] == 0x00 && packet[1] == 0x00 && packet[2] == 0x01);
    // SPS NAL header: nal_ref_idc=3 << 5 | type 7 = 0x67.
    NEMU_TEST_ASSERT(packet[3] == 0x67);
    // PPS NAL follows: search for the second start code with type 8 (0x68).
    bool saw_pps = false;
    for (size_t i = config_size / 2; i + 3 < config_size; ++i) {
        if (packet[i] == 0x00 && packet[i + 1] == 0x00 && packet[i + 2] == 0x01 &&
            packet[i + 3] == 0x68) { saw_pps = true; break; }
    }
    NEMU_TEST_ASSERT(saw_pps && "PPS NAL must be present in the composed header");

    // Pass-through path: second frame with frame_number != 0.
    u64 flags5 = flags | (5ULL << 46);
    vmem.WriteBlock(pic_info_va + 0xB0, &flags5, 8);
    std::vector<u8> packet2;
    size_t config2 = 0xFF;
    const bool ok2 = composer.ComposeFrame(regs, packet2, &config2, /*is_first=*/false);
    NEMU_TEST_ASSERT(ok2);
    NEMU_TEST_ASSERT(config2 == 0 && "pass-through frames have no header");
    NEMU_TEST_ASSERT(packet2.size() == 16);
    NEMU_TEST_ASSERT(packet2[0] == 0xA0 && packet2[15] == 0xAF);
    std::puts("  PASS TestH264Composer");
}

int main() {
    std::puts("========================================");
    std::puts("       NEMU NVDEC ENGINE TESTS          ");
    std::puts("========================================");
    TestNvdecRegisterWrite();
    TestNvdecDeviceIoctls();
    std::fprintf(stderr,"[PROBE] entering SubmitRouting\n"); TestNvdecSubmitRouting();
    std::fprintf(stderr,"[PROBE] entering H264Composer\n"); TestH264Composer();
    std::puts("ALL NVDEC TESTS PASSED SUCCESSFULLY!");
    return 0;
}
