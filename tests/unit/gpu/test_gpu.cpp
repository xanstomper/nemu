#include "core/gpu/deswizzle.hpp"
#include "core/gpu/null_backend.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/gpu_factory.hpp"
#include "core/gpu/nvhost/nvdevice.hpp"
#include "core/gpu/presentation/nvnflinger.hpp"
#include "core/gpu/texture/astc_decoder.hpp"
#include "core/gpu/shader/maxwell_shader_decoder.hpp"
#include "core/memory/virtual_memory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

void TestTextureSwizzleRoundTrip(u32 width, u32 height, u32 bpp, u32 block_height) {
    const size_t linear_size = static_cast<size_t>(width) * height * bpp;
    std::vector<u8> original(linear_size);

    // Populate with deterministic pattern
    for (size_t i = 0; i < linear_size; ++i) {
        original[i] = static_cast<u8>((i * 17 + 31) & 0xFF);
    }

    // Allocate swizzled buffer (aligned to block dimensions)
    const u32 width_bytes = width * bpp;
    const u32 blocks_x = (width_bytes + TextureSwizzler::GOB_WIDTH_BYTES - 1) / TextureSwizzler::GOB_WIDTH_BYTES;
    const u32 lines_per_block = TextureSwizzler::GOB_HEIGHT_LINES * block_height;
    const u32 blocks_y = (height + lines_per_block - 1) / lines_per_block;
    const size_t swizzled_size = static_cast<size_t>(blocks_x) * blocks_y * (TextureSwizzler::GOB_SIZE_BYTES * block_height);

    std::vector<u8> swizzled(swizzled_size, 0);
    std::vector<u8> recovered(linear_size, 0);

    // 1. Swizzle
    bool swizzle_res = TextureSwizzler::SwizzleBlockLinear(original, swizzled, width, height, bpp, block_height);
    NEMU_TEST_ASSERT(swizzle_res, "SwizzleBlockLinear failed");

    // Verify swizzled buffer is not identical to linear buffer (confirming permutation occurred)
    bool is_different = false;
    for (size_t i = 0; i < linear_size; ++i) {
        if (original[i] != swizzled[i]) {
            is_different = true;
            break;
        }
    }
    NEMU_TEST_ASSERT(is_different, "Swizzled data must differ from linear layout");

    // 2. Deswizzle
    bool deswizzle_res = TextureSwizzler::DeswizzleBlockLinear(swizzled, recovered, width, height, bpp, block_height);
    NEMU_TEST_ASSERT(deswizzle_res, "DeswizzleBlockLinear failed");

    // 3. Compare recovered data with original
    NEMU_TEST_ASSERT(original == recovered, "Round-trip deswizzled data must match original exactly");
}

int main() {
    std::cout << "[Test: Tegra X1 Maxwell GPU & Texture Pipeline]" << std::endl;

    // 1. Test Block-Linear Swizzling & Deswizzling across various dimensions and block heights
    {
        TestTextureSwizzleRoundTrip(64, 64, 4, 1);
        TestTextureSwizzleRoundTrip(128, 128, 4, 2);
        TestTextureSwizzleRoundTrip(256, 128, 4, 4);
        TestTextureSwizzleRoundTrip(128, 64, 2, 1); // 16-bit textures (e.g. RGB565)
        TestTextureSwizzleRoundTrip(64, 32, 1, 1);  // 8-bit textures
        std::cout << "  - Texture swizzling round-trip tests: PASSED" << std::endl;
    }

    // 2. Test Maxwell 3D Command Processor & Backend
    {
        auto backend = std::make_shared<NullGpuBackend>();
        NEMU_TEST_ASSERT(backend->Initialize(1280, 720), "Null backend init");

        Maxwell3D maxwell(backend);

        backend->BeginFrame();

        // Pushbuffer commands:
        // Set clear color to RGBA(1.0, 0.5, 0.25, 1.0)
        // Method ClearColorR = 0x0368 (inc count=4 for R, G, B, A)
        // Packet: (0 << 29) | (4 << 16) | 0x0368
        const u32 clear_r = 0x3F800000; // 1.0f
        const u32 clear_g = 0x3F000000; // 0.5f
        const u32 clear_b = 0x3E800000; // 0.25f
        const u32 clear_a = 0x3F800000; // 1.0f

        const u32 pushbuffer[] = {
            (4 << 16) | 0x0368, // 4 inc methods: ClearColorR, G, B, A
            clear_r,
            clear_g,
            clear_b,
            clear_a,
            (1 << 16) | 0x036C, // ClearSurface
            0x00000001,
            (1 << 16) | 0x0674, // DrawArrays: 3 vertices, Triangles (3)
            (3 << 8) | 3,
            (1 << 16) | 0x0674, // DrawArrays: 6 vertices, Triangles (3)
            (6 << 8) | 3
        };

        maxwell.SubmitPushbuffer(pushbuffer);

        backend->EndFrame();
        backend->Present();

        const auto stats = backend->GetStats();
        NEMU_TEST_ASSERT(stats.frames_presented == 1, "Frames presented must be 1");
        NEMU_TEST_ASSERT(stats.draw_calls == 2, "Draw calls must be 2");
        NEMU_TEST_ASSERT(stats.vertices_submitted == 9, "Vertices submitted must be 9 (3 + 6)");

        const auto clear_col = backend->GetLastClearColor();
        NEMU_TEST_ASSERT(clear_col.r == 1.0f, "Clear color R mismatch");
        NEMU_TEST_ASSERT(clear_col.g == 0.5f, "Clear color G mismatch");
        NEMU_TEST_ASSERT(clear_col.b == 0.25f, "Clear color B mismatch");
        NEMU_TEST_ASSERT(clear_col.a == 1.0f, "Clear color A mismatch");

        backend->Shutdown();
        std::cout << "  - Maxwell 3D pushbuffer processing tests: PASSED" << std::endl;
    }

    // 3. Test GpuFactory
    {
        auto factory_backend = GpuFactory::CreateBackend(1280, 720);
        NEMU_TEST_ASSERT(factory_backend != nullptr, "Factory must create backend");
        std::cout << "  - GpuFactory instantiated backend: " << factory_backend->GetBackendName() << std::endl;
        factory_backend->Shutdown();
    }

    // 3b. Software rasterizer: bind a colored triangle, draw it into the host
    // framebuffer, verify filled + background pixels, and dump a viewable PPM.
    {
        NullGpuBackend sw;
        NEMU_TEST_ASSERT(sw.Initialize(128, 128), "SW rasterizer init");

        // Full-frame triangle covering NDC [-1,1]x[-1,1], bright red.
        const RasterVertex tri[3] = {
            {-1.05f, -1.05f, 1.0f, 0.0f, 0.0f, 1.0f},
            { 1.05f, -1.05f, 1.0f, 0.0f, 0.0f, 1.0f},
            { 0.0f,   1.05f, 1.0f, 0.0f, 0.0f, 1.0f}
        };
        sw.BeginFrame();
        sw.ClearRenderTarget({0.0f, 0.0f, 0.0f, 1.0f}); // black background
        sw.SetRasterVertices(std::span<const RasterVertex>(tri, 3));
        sw.DrawArrays(PrimitiveTopology::Triangles, 0, 3);
        sw.EndFrame();
        sw.Present();

        // Middle of the triangle (x=64, y=64) must be bright red.
        const size_t mid = ((64u * 128u) + 64u) * 4u;
        const u8* fb = sw.Framebuffer();
        NEMU_TEST_ASSERT(fb[mid + 0] > 200, "SW rasterizer: center pixel must be red (R)");
        NEMU_TEST_ASSERT(fb[mid + 1] < 50 && fb[mid + 2] < 50, "SW rasterizer: center must not be green/blue");

        // A corner outside the triangle must remain background (black).
        const size_t corner = ((2u * 128u) + 2u) * 4u;
        NEMU_TEST_ASSERT(fb[corner + 0] < 50, "SW rasterizer: corner must be background (not red)");

        // Verify a real, dumpable image is produced.
        const char* path = "/tmp/nemu_software_triangle.ppm";
        NEMU_TEST_ASSERT(sw.DumpFramePPM(path), "SW rasterizer: PPM dump");
        std::FILE* f = std::fopen(path, "rb");
        NEMU_TEST_ASSERT(f != nullptr, "SW rasterizer: PPM file opened");
        char magic[3] = {0};
        if (f) { std::fread(magic, 1, 2, f); std::fclose(f); }
        NEMU_TEST_ASSERT(magic[0] == 'P' && magic[1] == '6', "SW rasterizer: P6 magic");

        sw.Shutdown();
        std::cout << "  - Software rasterizer rendered a first frame + PPM dump: PASSED" << std::endl;
    }

    // 4. Test NvMap
    {
        nvhost::NvMap nvmap;
        const u32 h1 = nvmap.Create(0x20000);
        const u32 h2 = nvmap.Create(0x40000);
        NEMU_TEST_ASSERT(h1 != 0 && h2 != 0 && h1 != h2, "NvMap::Create handles");

        NEMU_TEST_ASSERT(nvmap.Alloc(h1, 0, 0, 4096, 0x1, 0x1000'0000ULL), "NvMap::Alloc h1");
        NEMU_TEST_ASSERT(nvmap.Alloc(h2, 0, 0, 4096, 0x2, 0x2000'0000ULL), "NvMap::Alloc h2");

        u32 size_val = 0;
        NEMU_TEST_ASSERT(nvmap.GetParam(h1, 1, size_val) && size_val == 0x20000, "NvMap::GetParam size");

        u32 kind_val = 0;
        NEMU_TEST_ASSERT(nvmap.GetParam(h2, 5, kind_val) && kind_val == 2, "NvMap::GetParam kind");

        const u32 id1 = nvmap.GetId(h1);
        NEMU_TEST_ASSERT(id1 != 0, "NvMap::GetId");
        NEMU_TEST_ASSERT(nvmap.GetObjectById(id1)->handle == h1, "NvMap::GetObjectById");

        NEMU_TEST_ASSERT(nvmap.Free(h1), "NvMap::Free h1");
        NEMU_TEST_ASSERT(nvmap.GetObject(h1) == nullptr, "Freed handle must be null");
        std::cout << "  - NvMap memory handle manager tests: PASSED" << std::endl;
    }

    // 5. Test SyncpointManager
    {
        nvhost::SyncpointManager syncpoints;
        NEMU_TEST_ASSERT(syncpoints.Read(1) == 0, "Initial syncpoint must be 0");

        const u32 val1 = syncpoints.Increment(1);
        NEMU_TEST_ASSERT(val1 == 1, "Syncpoint increment to 1");
        NEMU_TEST_ASSERT(syncpoints.Read(1) == 1, "Syncpoint read 1");

        syncpoints.Increment(1, 4);
        NEMU_TEST_ASSERT(syncpoints.Read(1) == 5, "Syncpoint increment +4 -> 5");

        NEMU_TEST_ASSERT(syncpoints.IsSatisfied(1, 3), "IsSatisfied threshold <= current");
        NEMU_TEST_ASSERT(syncpoints.IsSatisfied(1, 5), "IsSatisfied threshold == current");
        NEMU_TEST_ASSERT(!syncpoints.IsSatisfied(1, 6), "IsSatisfied threshold > current");

        NEMU_TEST_ASSERT(syncpoints.Wait(1, 5, 10), "Syncpoint wait satisfied");
        NEMU_TEST_ASSERT(!syncpoints.Wait(1, 10, 5), "Syncpoint wait timeout");

        syncpoints.RegisterUserEvent(0x100, 1, 5);
        NEMU_TEST_ASSERT(syncpoints.CheckUserEvent(0x100), "User event triggered");
        std::cout << "  - SyncpointManager syncpoint and fence tests: PASSED" << std::endl;
    }

    // 6. Test AddressSpace
    {
        auto nvmap = std::make_shared<nvhost::NvMap>();
        const u32 h = nvmap->Create(0x10000);
        nvmap->Alloc(h, 0, 0, 4096, 0, 0x8000'0000ULL);

        nvhost::AddressSpace as(nvmap);
        const u64 gpu_va = as.MapBufferEx(h, 0, 4096);
        NEMU_TEST_ASSERT(gpu_va != 0, "MapBufferEx returned gpu_va");

        auto guest_va = as.GpuVaToGuestVa(gpu_va + 0x100);
        NEMU_TEST_ASSERT(guest_va.has_value() && *guest_va == 0x8000'0100ULL, "GpuVaToGuestVa translation");

        NEMU_TEST_ASSERT(as.UnmapBuffer(gpu_va), "UnmapBuffer");
        NEMU_TEST_ASSERT(!as.GpuVaToGuestVa(gpu_va).has_value(), "GpuVa must be unmapped");
        std::cout << "  - AddressSpace GPU VA mapping tests: PASSED" << std::endl;
    }

    // 7. Test Channel & GPFIFO submission to Maxwell3D
    {
        memory::VirtualMemory mem;
        NEMU_TEST_ASSERT(mem.Map(0x1000'0000ULL, 0x20000, memory::MemoryPermission::All), "Map guest memory");

        auto backend = std::make_shared<NullGpuBackend>();
        backend->Initialize(1280, 720);
        auto maxwell = std::make_shared<Maxwell3D>(backend);

        auto nvmap = std::make_shared<nvhost::NvMap>();
        auto syncpoints = std::make_shared<nvhost::SyncpointManager>();
        auto as = std::make_shared<nvhost::AddressSpace>(nvmap);

        const u32 h = nvmap->Create(0x10000);
        nvmap->Alloc(h, 0, 0, 4096, 0, 0x1000'0000ULL);
        const u64 pushbuf_gpu_va = as->MapBufferEx(h, 0, 4096);

        // Write pushbuffer commands into guest memory at 0x1000'0000ULL:
        // DrawArrays (3 vertices, triangles)
        const u32 push_cmds[] = {
            (1 << 16) | 0x0674, // DrawArrays: 3 vertices
            (3 << 8) | 3
        };
        mem.WriteBlock(0x1000'0000ULL, push_cmds, sizeof(push_cmds));

        nvhost::Channel channel(0, syncpoints, as, maxwell, &mem);
        channel.SetNvhostPid(1234);
        NEMU_TEST_ASSERT(channel.GetNvhostPid() == 1234, "Channel host PID");

        // Submit GPFIFO entry: [num_words:21][gpu_va:40]
        const u64 entry = (pushbuf_gpu_va & 0xFF'FFFF'FFFFULL) | (static_cast<u64>(2) << 42);
        const u64 entries[] = { entry };

        auto [fence_id, fence_val] = channel.SubmitGpfifo(entries, 0);
        NEMU_TEST_ASSERT(fence_id == 0, "Fence id == channel id");
        NEMU_TEST_ASSERT(fence_val == 1, "Fence val incremented to 1");

        const auto stats = backend->GetStats();
        NEMU_TEST_ASSERT(stats.draw_calls == 1, "Maxwell processed GPFIFO draw call");
        NEMU_TEST_ASSERT(stats.vertices_submitted == 3, "3 vertices submitted");
        backend->Shutdown();
        std::cout << "  - Channel GPFIFO pushbuffer submission tests: PASSED" << std::endl;
    }

    // 8. Test NvDeviceManager
    {
        memory::VirtualMemory mem;
        auto backend = std::make_shared<NullGpuBackend>();
        backend->Initialize(1280, 720);
        auto maxwell = std::make_shared<Maxwell3D>(backend);

        nvhost::NvDeviceManager dev_mgr(maxwell, &mem);
        const s32 fd_nvmap = dev_mgr.Open("/dev/nvmap");
        const s32 fd_ctrl  = dev_mgr.Open("/dev/nvhost-ctrl");
        const s32 fd_as    = dev_mgr.Open("/dev/nvhost-as-gpu");
        const s32 fd_gpu   = dev_mgr.Open("/dev/nvhost-gpu");
        NEMU_TEST_ASSERT(fd_nvmap > 0 && fd_ctrl > 0 && fd_as > 0 && fd_gpu > 0, "Open nvhost devices");

        // Test NVMAP_IOC_CREATE via Ioctl
        struct CreateArgs { u32 size; u32 handle; } c_args{0x4000, 0};
        std::vector<u8> in_buf(reinterpret_cast<u8*>(&c_args), reinterpret_cast<u8*>(&c_args) + sizeof(c_args));
        std::vector<u8> out_buf(sizeof(c_args), 0);

        const u32 res = dev_mgr.Ioctl(fd_nvmap, 0xC0180101, in_buf, out_buf);
        NEMU_TEST_ASSERT(res == 0, "NVMAP_IOC_CREATE ioctl success");
        CreateArgs c_out{};
        std::memcpy(&c_out, out_buf.data(), sizeof(c_out));
        NEMU_TEST_ASSERT(c_out.handle != 0, "Created nvmap handle");

        NEMU_TEST_ASSERT(dev_mgr.Close(fd_nvmap), "Close nvmap fd");
        NEMU_TEST_ASSERT(dev_mgr.Close(fd_ctrl), "Close ctrl fd");
        NEMU_TEST_ASSERT(dev_mgr.Close(fd_as), "Close as fd");
        NEMU_TEST_ASSERT(dev_mgr.Close(fd_gpu), "Close gpu fd");
        backend->Shutdown();
        std::cout << "  - NvDeviceManager ioctl dispatch tests: PASSED" << std::endl;
    }

    // 9. Test BufferQueue
    {
        presentation::BufferQueue bq;
        bq.Connect();
        NEMU_TEST_ASSERT(bq.IsConnected(), "BufferQueue connected");

        const s32 slot = bq.DequeueBuffer(1280, 720, PixelFormat::R8G8B8A8_UNORM);
        NEMU_TEST_ASSERT(slot >= 0 && slot < 64, "DequeueBuffer returned valid slot");

        auto slot_info = bq.GetSlot(slot);
        NEMU_TEST_ASSERT(slot_info.has_value() && slot_info->state == presentation::BufferState::Dequeued, "Slot dequeued");

        std::vector<u8> pixel_dummy(1280 * 720 * 4, 0xFF);
        NEMU_TEST_ASSERT(bq.QueueBuffer(slot, 1'000'000, 0x8000'0000ULL, pixel_dummy), "QueueBuffer");
        NEMU_TEST_ASSERT(bq.GetQueuedCount() == 1, "Queued count == 1");

        auto acquired = bq.AcquireBuffer();
        NEMU_TEST_ASSERT(acquired.has_value() && *acquired == slot, "AcquireBuffer");
        NEMU_TEST_ASSERT(bq.GetQueuedCount() == 0, "Queued count == 0 after acquire");

        NEMU_TEST_ASSERT(bq.ReleaseBuffer(slot), "ReleaseBuffer");
        slot_info = bq.GetSlot(slot);
        NEMU_TEST_ASSERT(slot_info.has_value() && slot_info->state == presentation::BufferState::Free, "Slot back to free");

        bq.Disconnect();
        NEMU_TEST_ASSERT(!bq.IsConnected(), "BufferQueue disconnected");
        std::cout << "  - BufferQueue state machine tests: PASSED" << std::endl;
    }

    // 10. Test Nvnflinger Compositor
    {
        auto backend = std::make_shared<NullGpuBackend>();
        backend->Initialize(1280, 720);

        presentation::Nvnflinger flinger(backend);
        const u64 disp = flinger.OpenDisplay("Default");
        NEMU_TEST_ASSERT(disp != 0, "OpenDisplay Default");

        const u64 layer = flinger.CreateLayer(disp);
        NEMU_TEST_ASSERT(layer != 0, "CreateLayer");

        auto bq = flinger.GetBufferQueue(layer);
        NEMU_TEST_ASSERT(bq != nullptr, "GetBufferQueue");

        const s32 slot = bq->DequeueBuffer(1280, 720, PixelFormat::R8G8B8A8_UNORM);
        NEMU_TEST_ASSERT(slot >= 0, "DequeueBuffer on layer");
        bq->QueueBuffer(slot, 2'000'000, 0);

        const bool presented = flinger.ComposeAndPresent();
        NEMU_TEST_ASSERT(presented, "ComposeAndPresent succeeded");
        NEMU_TEST_ASSERT(flinger.GetTotalFramesPresented() == 1, "Total frames presented == 1");

        NEMU_TEST_ASSERT(flinger.DestroyLayer(layer), "DestroyLayer");
        backend->Shutdown();
        std::cout << "  - Nvnflinger display compositor tests: PASSED" << std::endl;
    }

    // 11. Test ASTC Texture Decoder
    {
        using namespace nemu::core::gpu::texture;

        // A. Test sRGB curve conversion properties (endpoints and monotonic)
        NEMU_TEST_ASSERT(AstcDecoder::SrgbToLinear(0) == 0, "SrgbToLinear(0) == 0");
        NEMU_TEST_ASSERT(AstcDecoder::SrgbToLinear(255) == 255, "SrgbToLinear(255) == 255");
        NEMU_TEST_ASSERT(AstcDecoder::LinearToSrgb(0) == 0, "LinearToSrgb(0) == 0");
        NEMU_TEST_ASSERT(AstcDecoder::LinearToSrgb(255) == 255, "LinearToSrgb(255) == 255");

        for (u32 c = 1; c < 256; ++c) {
            NEMU_TEST_ASSERT(AstcDecoder::SrgbToLinear(static_cast<u8>(c)) >=
                             AstcDecoder::SrgbToLinear(static_cast<u8>(c - 1)),
                             "SrgbToLinear must be monotonic");
            NEMU_TEST_ASSERT(AstcDecoder::LinearToSrgb(static_cast<u8>(c)) >=
                             AstcDecoder::LinearToSrgb(static_cast<u8>(c - 1)),
                             "LinearToSrgb must be monotonic");
        }

        // B. Test Void-Extent block decoding (solid color block)
        // In ASTC void-extent: bits[1:0]=0b11, bits[8:7]=0b11, bit 9=0 (LDR)
        // RGBA16 at bits 64..127. Let's create a solid teal block: R=0x00, G=0xFF, B=0xFF, A=0xFF
        std::array<u8, 16> void_block{};
        void_block[0] = 0x03; // bits 0..1 = 11
        void_block[1] = 0x01; // bit 7 = 0, bit 8 = 1? Let's ensure bits 7..8 are 11:
        // bit 7 is void_block[0] bit 7, bit 8 is void_block[1] bit 0
        void_block[0] |= (1 << 7);
        void_block[1] |= (1 << 0);
        // Bit 9 (LDR) is void_block[1] bit 1 (0)

        // Set 16-bit color values at bytes 8..15 (bits 64..127)
        void_block[8] = 0x00;  void_block[9] = 0x20;   // R = 0x2000 (R8 = 0x20)
        void_block[10] = 0x00; void_block[11] = 0x80;  // G = 0x8000 (G8 = 0x80)
        void_block[12] = 0x00; void_block[13] = 0xC0;  // B = 0xC000 (B8 = 0xC0)
        void_block[14] = 0x00; void_block[15] = 0xFF;  // A = 0xFF00 (A8 = 0xFF)

        std::array<u32, 16> out_pixels_4x4{};
        bool decoded = AstcDecoder::DecodeBlock(void_block, 4, 4, out_pixels_4x4, false);
        NEMU_TEST_ASSERT(decoded, "DecodeBlock void extent 4x4");

        for (u32 p = 0; p < 16; ++p) {
            u32 pixel = out_pixels_4x4[p];
            u8 r = static_cast<u8>(pixel & 0xFF);
            u8 g = static_cast<u8>((pixel >> 8) & 0xFF);
            u8 b = static_cast<u8>((pixel >> 16) & 0xFF);
            u8 a = static_cast<u8>((pixel >> 24) & 0xFF);
            NEMU_TEST_ASSERT(r == 0x20, "Void extent R component mismatch");
            NEMU_TEST_ASSERT(g == 0x80, "Void extent G component mismatch");
            NEMU_TEST_ASSERT(b == 0xC0, "Void extent B component mismatch");
            NEMU_TEST_ASSERT(a == 0xFF, "Void extent A component mismatch");
        }

        // C. Test DecompressSurface on a 8x8 surface with 4x4 blocks (4 blocks total)
        std::vector<u8> surface_data(4 * AstcDecoder::BLOCK_SIZE_BYTES, 0);
        // Fill all 4 blocks with our void block
        for (int i = 0; i < 4; ++i) {
            std::memcpy(surface_data.data() + i * 16, void_block.data(), 16);
        }

        std::vector<u32> out_surface;
        bool surface_ok = AstcDecoder::DecompressSurface(surface_data, 8, 8, 4, 4, out_surface, false);
        NEMU_TEST_ASSERT(surface_ok, "DecompressSurface 8x8 with 4x4 ASTC blocks");
        NEMU_TEST_ASSERT(out_surface.size() == 64, "Out surface size == 64");
        NEMU_TEST_ASSERT(out_surface[0] == out_pixels_4x4[0], "Surface pixel matches block pixel");
        NEMU_TEST_ASSERT(out_surface[63] == out_pixels_4x4[15], "Corner pixel matches block pixel");

        std::cout << "  - ASTC compressed texture decoder tests: PASSED" << std::endl;
    }

    // 12. Test Maxwell Shader Bytecode Decompiler (SASS -> HLSL / GLSL)
    {
        using namespace nemu::core::gpu::shader;

        // Synthesize a Maxwell vertex shader bytecode stream:
        // 1. MOV R0, R1
        // 2. FADD R2, R0, R3
        // 3. FMUL R4, R2, R5
        // 4. FFMA R6, R4, R7, R8
        // 5. ST.ATTR a[0].x, R6 (output position x)
        // 6. EXIT
        std::vector<u8> code(48, 0);

        auto EncodeInst = [](std::span<u8> dst, u32 opcode, u32 rd, u32 ra, u32 rb, u32 rc) {
            u64 val = (static_cast<u64>(opcode) << 52) |
                      (static_cast<u64>(rd) & 0xFF) |
                      ((static_cast<u64>(ra) & 0xFF) << 8) |
                      (static_cast<u64>(7) << 16) | // Predicate 7 (Always True)
                      ((static_cast<u64>(rb) & 0xFF) << 20) |
                      ((static_cast<u64>(rc) & 0xFF) << 32);
            std::memcpy(dst.data(), &val, sizeof(u64));
        };

        EncodeInst(std::span<u8>(code.data() + 0, 8), 0x5C0, 0, 1, 0, 0);   // MOV R0, R1
        EncodeInst(std::span<u8>(code.data() + 8, 8), 0x580, 2, 0, 3, 0);   // FADD R2, R0, R3
        EncodeInst(std::span<u8>(code.data() + 16, 8), 0x582, 4, 2, 5, 0);  // FMUL R4, R2, R5
        EncodeInst(std::span<u8>(code.data() + 24, 8), 0x583, 6, 4, 7, 8);  // FFMA R6, R4, R7, R8
        EncodeInst(std::span<u8>(code.data() + 32, 8), 0x5B1, 0, 6, 0, 0);  // ST.ATTR a[0].x, R6
        EncodeInst(std::span<u8>(code.data() + 40, 8), 0x5D1, 0, 0, 0, 0);  // EXIT

        auto decomp = MaxwellShaderDecoder::DecodeAndDecompile(code, ShaderStage::Vertex);
        NEMU_TEST_ASSERT(decomp.instructions.size() == 6, "Must decode 6 instructions");
        NEMU_TEST_ASSERT(decomp.instructions[0].opcode == MaxwellOpcode::MOV, "Inst 0 must be MOV");
        NEMU_TEST_ASSERT(decomp.instructions[1].opcode == MaxwellOpcode::FADD, "Inst 1 must be FADD");
        NEMU_TEST_ASSERT(decomp.instructions[2].opcode == MaxwellOpcode::FMUL, "Inst 2 must be FMUL");
        NEMU_TEST_ASSERT(decomp.instructions[3].opcode == MaxwellOpcode::FFMA, "Inst 3 must be FFMA");
        NEMU_TEST_ASSERT(decomp.instructions[4].opcode == MaxwellOpcode::ST_ATTR, "Inst 4 must be ST_ATTR");
        NEMU_TEST_ASSERT(decomp.instructions[5].opcode == MaxwellOpcode::EXIT, "Inst 5 must be EXIT");

        // Verify emitted HLSL contains target structures and operators
        NEMU_TEST_ASSERT(decomp.hlsl_source.find("struct VSInput") != std::string::npos, "HLSL has VSInput");
        NEMU_TEST_ASSERT(decomp.hlsl_source.find("struct VSOutput") != std::string::npos, "HLSL has VSOutput");
        NEMU_TEST_ASSERT(decomp.hlsl_source.find("output.out_pos.x") != std::string::npos, "HLSL has position out");
        NEMU_TEST_ASSERT(decomp.hlsl_source.find("return output;") != std::string::npos, "HLSL has return");

        // Verify emitted GLSL
        NEMU_TEST_ASSERT(decomp.glsl_source.find("gl_Position") != std::string::npos, "GLSL has gl_Position");

        std::cout << "  - Maxwell SM 5.3 shader decompiler (HLSL SM 6.0/5.1) tests: PASSED" << std::endl;
    }

    std::cout << "[Test: Tegra X1 Maxwell GPU & Texture Pipeline PASSED]" << std::endl;
    return 0;
}
