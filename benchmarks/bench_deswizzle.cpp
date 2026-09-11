#include "bench.hpp"
#include "core/gpu/deswizzle.hpp"
#include <iostream>
#include <vector>

using namespace nemu;
using namespace nemu::core;

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  NEMU BENCHMARK: Tegra X1 GM20B Block-Linear Deswizzler " << std::endl;
    std::cout << "=========================================================" << std::endl;

    constexpr u32 WIDTH = 1280;
    constexpr u32 HEIGHT = 720;
    constexpr u32 BYTES_PER_PIXEL = 4; // RGBA8
    constexpr size_t FRAME_SIZE = WIDTH * HEIGHT * BYTES_PER_PIXEL; // 3.6864 MB

    std::vector<u8> linear_buf(FRAME_SIZE, 0x7F);
    std::vector<u8> block_linear_buf(FRAME_SIZE, 0);
    std::vector<u8> round_trip_buf(FRAME_SIZE, 0);

    // Initial swizzle
    gpu::TextureSwizzler::SwizzleBlockLinear(
        std::span<const u8>(linear_buf),
        std::span<u8>(block_linear_buf),
        WIDTH, HEIGHT, BYTES_PER_PIXEL, 1);

    constexpr u64 FRAMES = 300;
    constexpr u64 TOTAL_BYTES = FRAMES * FRAME_SIZE;

    {
        bench::BenchmarkTimer timer("GM20B 720p Block-Linear Deswizzle", FRAMES);

        for (u64 f = 0; f < FRAMES; ++f) {
            gpu::TextureSwizzler::DeswizzleBlockLinear(
                std::span<const u8>(block_linear_buf),
                std::span<u8>(round_trip_buf),
                WIDTH, HEIGHT, BYTES_PER_PIXEL, 1);
        }
    }

    const double mb_per_sec = (static_cast<double>(TOTAL_BYTES) / (1024.0 * 1024.0));
    std::cout << "  Total Processed: " << std::fixed << std::setprecision(2) << mb_per_sec << " MB" << std::endl;
    std::cout << "=========================================================" << std::endl;

    return 0;
}
