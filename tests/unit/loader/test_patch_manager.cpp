#include "core/loader/patch_manager.hpp"
#include "core/filesystem/vfs.hpp"
#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <cstdlib>

using namespace nemu;
using namespace nemu::core;

#define NEMU_TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

static void TestFormatBuildId() {
    std::array<u8, 0x20> build_id{};
    for (size_t i = 0; i < build_id.size(); ++i) {
        build_id[i] = static_cast<u8>(i);
    }
    std::string formatted = loader::PatchManager::FormatBuildId(build_id);
    NEMU_TEST_ASSERT(formatted.size() == 64);
    NEMU_TEST_ASSERT(formatted.substr(0, 8) == "00010203");
    std::puts("  PASS TestFormatBuildId");
}

static void TestParseIps() {
    // Construct standard 24-bit IPS stream:
    // "PATCH"
    // [0x00, 0x00, 0x10] (offset 16)
    // [0x00, 0x04]       (size 4)
    // [0x11, 0x22, 0x33, 0x44]
    // [0x00, 0x00, 0x20] (offset 32)
    // [0x00, 0x00]       (size 0 -> RLE)
    // [0x00, 0x05]       (rle_size 5)
    // [0xAA]             (rle_byte)
    // "EOF"
    std::vector<u8> ips_data = {
        'P', 'A', 'T', 'C', 'H',
        0x00, 0x00, 0x10,
        0x00, 0x04,
        0x11, 0x22, 0x33, 0x44,
        0x00, 0x00, 0x20,
        0x00, 0x00,
        0x00, 0x05,
        0xAA,
        'E', 'O', 'F'
    };

    auto patches = loader::PatchManager::ParseIps(ips_data);
    NEMU_TEST_ASSERT(patches.size() == 2);
    NEMU_TEST_ASSERT(patches[0].offset == 0x10);
    NEMU_TEST_ASSERT(patches[0].data.size() == 4);
    NEMU_TEST_ASSERT(patches[0].data[0] == 0x11 && patches[0].data[3] == 0x44);

    NEMU_TEST_ASSERT(patches[1].offset == 0x20);
    NEMU_TEST_ASSERT(patches[1].data.size() == 5);
    for (u8 b : patches[1].data) {
        NEMU_TEST_ASSERT(b == 0xAA);
    }
    std::puts("  PASS TestParseIps");
}

static void TestParseIps32() {
    // Construct 32-bit IPS32 stream:
    // "IPS32"
    // [0x00, 0x01, 0x00, 0x00] (offset 0x10000)
    // [0x00, 0x02]             (size 2)
    // [0xDE, 0xAD]
    // "EEOF"
    std::vector<u8> ips_data = {
        'I', 'P', 'S', '3', '2',
        0x00, 0x01, 0x00, 0x00,
        0x00, 0x02,
        0xDE, 0xAD,
        'E', 'E', 'O', 'F'
    };

    auto patches = loader::PatchManager::ParseIps(ips_data);
    NEMU_TEST_ASSERT(patches.size() == 1);
    NEMU_TEST_ASSERT(patches[0].offset == 0x10000);
    NEMU_TEST_ASSERT(patches[0].data.size() == 2);
    NEMU_TEST_ASSERT(patches[0].data[0] == 0xDE && patches[0].data[1] == 0xAD);
    std::puts("  PASS TestParseIps32");
}

static void TestApplyPatches() {
    std::vector<u8> buffer(0x40, 0x00);
    std::vector<loader::IpsPatch> patches = {
        {0x04, {0x42, 0x43}},
        {0x10, {0x99, 0x88, 0x77}}
    };

    NEMU_TEST_ASSERT(loader::PatchManager::ApplyPatches(buffer, patches));
    NEMU_TEST_ASSERT(buffer[0x04] == 0x42);
    NEMU_TEST_ASSERT(buffer[0x05] == 0x43);
    NEMU_TEST_ASSERT(buffer[0x10] == 0x99);
    NEMU_TEST_ASSERT(buffer[0x11] == 0x88);
    NEMU_TEST_ASSERT(buffer[0x12] == 0x77);
    std::puts("  PASS TestApplyPatches");
}

static void TestVfsIntegration() {
    filesystem::VirtualFileSystem vfs;
    vfs.Mount("sdmc:/", "./sdmc");

    std::array<u8, 0x20> build_id{};
    build_id[0] = 0xAA;
    build_id[1] = 0xBB;
    build_id[2] = 0xCC;
    std::string build_id_hex = loader::PatchManager::FormatBuildId(build_id);

    u64 title_id = 0x0100000000010000ULL;
    std::string patch_path = "sdmc:/atmosphere/contents/0100000000010000/exefs/" + build_id_hex + ".ips";

    // Synthetic IPS patch modifying offset 0x00 to 0x77
    std::vector<u8> ips_data = {
        'P', 'A', 'T', 'C', 'H',
        0x00, 0x00, 0x00,
        0x00, 0x01,
        0x77,
        'E', 'O', 'F'
    };

    vfs.WriteFile(patch_path, ips_data);

    loader::PatchManager pm(vfs);
    std::vector<u8> nso_image(0x100, 0x00);
    NEMU_TEST_ASSERT(pm.ApplyExeFsPatches(title_id, build_id, nso_image));
    NEMU_TEST_ASSERT(nso_image[0] == 0x77);
    std::puts("  PASS TestVfsIntegration");
}

int main() {
    std::puts("========================================");
    std::puts("      NEMU PATCH MANAGER TESTS          ");
    std::puts("========================================");
    TestFormatBuildId();
    TestParseIps();
    TestParseIps32();
    TestApplyPatches();
    TestVfsIntegration();
    std::puts("ALL PATCH MANAGER TESTS PASSED SUCCESSFULLY!");
    return 0;
}
