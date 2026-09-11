#include "core/save/save_manager.hpp"
#include "core/filesystem/vfs.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <cstdlib>

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

int main() {
    std::cout << "[Test: SaveManager Atomic Storage & Recovery]" << std::endl;

    const std::filesystem::path test_dir = std::filesystem::current_path() / "test_save_sandbox";
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
    std::filesystem::create_directories(test_dir, ec);

    filesystem::VirtualFileSystem vfs;
    NEMU_TEST_ASSERT(vfs.Mount("save:/", test_dir, false), "Mount save:/ to test sandbox");

    save::SaveManager save_mgr(vfs);
    const u64 TITLE_ID = 0x0100000000010000ULL;
    const std::string FILE_NAME = "game_slot0.sav";

    // Test 1: Checksum consistency
    {
        const std::string text1 = "Nintendo Switch Save Payload";
        std::span<const u8> span1(reinterpret_cast<const u8*>(text1.data()), text1.size());
        u64 c1 = save::SaveManager::CalculateChecksum(span1);
        u64 c2 = save::SaveManager::CalculateChecksum(span1);
        NEMU_TEST_ASSERT(c1 == c2, "Checksums of identical data must match");

        const std::string text2 = "Nintendo Switch Save Payloae";
        std::span<const u8> span2(reinterpret_cast<const u8*>(text2.data()), text2.size());
        u64 c3 = save::SaveManager::CalculateChecksum(span2);
        NEMU_TEST_ASSERT(c1 != c3, "Checksums of different data must not match");
        std::cout << "  - Checksum validation: PASSED" << std::endl;
    }

    // Test 2: Initial save write and read
    const std::string payload1 = "Initial Game State: Level 1, Gold: 50";
    std::span<const u8> data1(reinterpret_cast<const u8*>(payload1.data()), payload1.size());

    NEMU_TEST_ASSERT(save_mgr.WriteSaveData(TITLE_ID, FILE_NAME, data1), "Write initial save");

    auto read1 = save_mgr.ReadSaveData(TITLE_ID, FILE_NAME);
    NEMU_TEST_ASSERT(read1.has_value(), "Read initial save");
    NEMU_TEST_ASSERT(read1->size() == data1.size(), "Read save size match");
    NEMU_TEST_ASSERT(std::memcmp(read1->data(), data1.data(), data1.size()) == 0, "Read save content match");
    std::cout << "  - Initial save and read: PASSED" << std::endl;

    // Test 3: Backup rotation on subsequent write
    const std::string payload2 = "Updated Game State: Level 10, Gold: 5000";
    std::span<const u8> data2(reinterpret_cast<const u8*>(payload2.data()), payload2.size());

    NEMU_TEST_ASSERT(save_mgr.WriteSaveData(TITLE_ID, FILE_NAME, data2), "Write updated save");

    auto read2 = save_mgr.ReadSaveData(TITLE_ID, FILE_NAME);
    NEMU_TEST_ASSERT(read2.has_value(), "Read updated save");
    NEMU_TEST_ASSERT(read2->size() == data2.size(), "Updated save size match");
    NEMU_TEST_ASSERT(std::memcmp(read2->data(), data2.data(), data2.size()) == 0, "Updated save content match");
    std::cout << "  - Save update with backup rotation: PASSED" << std::endl;

    // Test 4: Corruption and automatic fallback recovery to .bak
    {
        // Corrupt primary save file
        auto primary_path = vfs.ResolvePath("save:/0100000000010000/" + FILE_NAME);
        NEMU_TEST_ASSERT(primary_path.has_value(), "Resolve primary path");

        auto raw_bytes = vfs.ReadFile("save:/0100000000010000/" + FILE_NAME);
        NEMU_TEST_ASSERT(raw_bytes.has_value(), "Read raw primary bytes");

        // Corrupt data byte
        (*raw_bytes)[6] ^= 0xFF;
        NEMU_TEST_ASSERT(vfs.WriteFile("save:/0100000000010000/" + FILE_NAME, *raw_bytes), "Write corrupted primary");

        // ReadSaveData should detect corruption and recover payload1 from .bak
        auto recovered = save_mgr.ReadSaveData(TITLE_ID, FILE_NAME);
        NEMU_TEST_ASSERT(recovered.has_value(), "Recover from backup upon corruption");
        NEMU_TEST_ASSERT(recovered->size() == data1.size(), "Recovered size matches payload1");
        NEMU_TEST_ASSERT(std::memcmp(recovered->data(), data1.data(), data1.size()) == 0, "Recovered data matches payload1");
        std::cout << "  - Corrupt primary auto-recovery from .bak: PASSED" << std::endl;
    }

    // Test 5: Delete save data
    NEMU_TEST_ASSERT(save_mgr.DeleteSaveData(TITLE_ID, FILE_NAME), "Delete save data");
    auto read_after_delete = save_mgr.ReadSaveData(TITLE_ID, FILE_NAME);
    NEMU_TEST_ASSERT(!read_after_delete.has_value(), "Save must not exist after deletion");
    std::cout << "  - Delete save data: PASSED" << std::endl;

    // Clean up
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "[Test: SaveManager Atomic Storage & Recovery PASSED]" << std::endl;
    return 0;
}
