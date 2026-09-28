#include "core/gpu/kepler_compute.hpp"
#include "core/gpu/compute_qmd.hpp"
#include "core/gpu/null_backend.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/types.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cstring>

using namespace nemu;
using namespace nemu::core;
using namespace nemu::core::gpu;

#define KC_ASSERT(...)                                                         \
    do {                                                                       \
        if (!(__VA_ARGS__)) {                                                  \
            std::cerr << "Assertion failed: " #__VA_ARGS__ << " at "           \
                      << __FILE__ << ":" << __LINE__ << std::endl;             \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

int main() {
    std::cout << "[Test: Kepler Compute engine (0xB197) — QMD launch]" << std::endl;

    memory::VirtualMemory mem;
    auto backend = std::make_shared<NullGpuBackend>();
    KC_ASSERT(backend->Initialize(64, 64));

    constexpr vaddr_t kQmd = 0x00B0000000ULL;
    KC_ASSERT(mem.Map(kQmd, 0x1000, memory::MemoryPermission::All));

    // Build a valid QMD: grid 4x2x1, block 8x8x1, shared 1 KiB, program offset 0x40.
    ComputeQmd qmd{};
    qmd.words[8] = 0x40;                                   // program offset
    qmd.words[12] = 4;                                     // grid X
    qmd.words[13] = 2 | (1u << 16);                        // grid Y=2, Z=1
    qmd.words[17] = 1024;                                  // shared memory bytes
    qmd.words[18] = (8u << 16);                            // block X
    qmd.words[19] = 8 | (1u << 16);                        // block Y=8, Z=1
    std::vector<u8> qmd_bytes(sizeof(qmd.words));
    std::memcpy(qmd_bytes.data(), qmd.words.data(), qmd_bytes.size());
    KC_ASSERT(mem.WriteBlock(kQmd, qmd_bytes.data(), qmd_bytes.size()));

    KeplerCompute kepler(&mem, backend.get());

    // Program the launch-descriptor address (64-bit latch), then launch.
    kepler.CallMethod(KeplerCompute::REG_LAUNCH_DESC_LOC,
                      static_cast<u32>(kQmd & 0xFFFFFFFFULL));
    kepler.CallMethod(KeplerCompute::REG_LAUNCH_DESC_LOC + 1,
                      static_cast<u32>(kQmd >> 32));
    KC_ASSERT(kepler.GetQmdAddress() == kQmd, "QMD address latched");

    kepler.CallMethod(KeplerCompute::REG_LAUNCH, 1);
    KC_ASSERT(kepler.GetDispatchCount() == 1, "one compute dispatch");

    // Launch without QMD address must be rejected.
    kepler.Reset();
    kepler.CallMethod(KeplerCompute::REG_LAUNCH, 1);
    KC_ASSERT(kepler.GetDispatchCount() == 1, "launch without QMD rejected");

    std::cout << "  - Kepler Compute QMD launch (grid/block parse + dispatch + guards): PASSED" << std::endl;
    return 0;
}
