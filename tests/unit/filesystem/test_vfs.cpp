#include "core/filesystem/vfs.hpp"
#include <iostream>
#include <vector>
#include <string>
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
using namespace nemu::core::filesystem;

int main() {
    std::cout << "[Test: Virtual File System (VFS) & Sandbox Security]" << std::endl;

    std::filesystem::path temp_base = std::filesystem::temp_directory_path() / "nemu_vfs_test";
    std::filesystem::remove_all(temp_base);
    std::filesystem::create_directories(temp_base / "sdmc");
    std::filesystem::create_directories(temp_base / "romfs");

    VirtualFileSystem vfs;

    // 1. Mount test
    NEMU_TEST_ASSERT(vfs.Mount("sdmc:/", temp_base / "sdmc", false), "Mount writable sdmc:/");
    NEMU_TEST_ASSERT(vfs.Mount("romfs:/", temp_base / "romfs", true), "Mount read-only romfs:/");

    NEMU_TEST_ASSERT(vfs.IsMounted("sdmc:/"), "sdmc:/ must be mounted");
    NEMU_TEST_ASSERT(vfs.IsMounted("sdmc"), "sdmc prefix variation must be recognized");
    NEMU_TEST_ASSERT(vfs.IsMounted("romfs:/"), "romfs:/ must be mounted");
    NEMU_TEST_ASSERT(!vfs.IsMounted("nonexistent:/"), "nonexistent must not be mounted");

    NEMU_TEST_ASSERT(!vfs.IsReadOnly("sdmc:/"), "sdmc must be writable");
    NEMU_TEST_ASSERT(vfs.IsReadOnly("romfs:/"), "romfs must be read-only");

    // 2. Sandbox Security Tests (Path Traversal Prevention)
    {
        auto res1 = vfs.ResolvePath("sdmc:/../../etc/passwd");
        NEMU_TEST_ASSERT(!res1.has_value(), "Direct traversal ../.. must be blocked");

        auto res2 = vfs.ResolvePath("sdmc:/nested/dir/../../../../escaped.txt");
        NEMU_TEST_ASSERT(!res2.has_value(), "Deep nested traversal must be blocked");

        auto res3 = vfs.ResolvePath("sdmc:/..\\..\\windows\\system32");
        NEMU_TEST_ASSERT(!res3.has_value(), "Backslash traversal must be blocked");

        auto res4 = vfs.ResolvePath("invalid_path_without_colon");
        NEMU_TEST_ASSERT(!res4.has_value(), "Invalid format must be blocked");

        auto res5 = vfs.ResolvePath("unknown:/file.txt");
        NEMU_TEST_ASSERT(!res5.has_value(), "Unregistered mount must be blocked");
    }

    // 3. Directory creation and checks
    {
        NEMU_TEST_ASSERT(vfs.CreateDirectories("sdmc:/games/homebrew/data"), "Create directories in sdmc");
        NEMU_TEST_ASSERT(vfs.DirectoryExists("sdmc:/games/homebrew/data"), "Directory must exist");
        NEMU_TEST_ASSERT(!vfs.CreateDirectories("romfs:/illegal_dir"), "Creating dir in read-only mount must fail");
    }

    // 4. File Write & Read operations
    {
        const std::string test_data = "Nemu Switch Emulator - Real Homebrew Execution";
        std::span<const u8> data_span(reinterpret_cast<const u8*>(test_data.data()), test_data.size());

        NEMU_TEST_ASSERT(vfs.WriteFile("sdmc:/games/homebrew/data/info.txt", data_span), "Write file to sdmc");
        NEMU_TEST_ASSERT(vfs.FileExists("sdmc:/games/homebrew/data/info.txt"), "File must exist");

        auto size_opt = vfs.GetFileSize("sdmc:/games/homebrew/data/info.txt");
        NEMU_TEST_ASSERT(size_opt.has_value() && *size_opt == test_data.size(), "File size must match");

        auto read_data_opt = vfs.ReadFile("sdmc:/games/homebrew/data/info.txt");
        NEMU_TEST_ASSERT(read_data_opt.has_value(), "ReadFile must succeed");
        std::string read_str(reinterpret_cast<const char*>(read_data_opt->data()), read_data_opt->size());
        NEMU_TEST_ASSERT(read_str == test_data, "Read data must match written data");

        // Attempt write on read-only mount
        NEMU_TEST_ASSERT(!vfs.WriteFile("romfs:/cannot_write.bin", data_span), "Writing to read-only mount must fail");
    }

    // 5. File deletion
    {
        NEMU_TEST_ASSERT(vfs.DeleteFile("sdmc:/games/homebrew/data/info.txt"), "Delete file must succeed");
        NEMU_TEST_ASSERT(!vfs.FileExists("sdmc:/games/homebrew/data/info.txt"), "Deleted file must not exist");
        NEMU_TEST_ASSERT(!vfs.DeleteFile("sdmc:/games/homebrew/data/info.txt"), "Deleting non-existent file returns false");
    }

    // 6. Unmount
    NEMU_TEST_ASSERT(vfs.Unmount("sdmc:/"), "Unmount sdmc");
    NEMU_TEST_ASSERT(!vfs.IsMounted("sdmc:/"), "sdmc must no longer be mounted");

    // Clean up temp
    std::filesystem::remove_all(temp_base);

    std::cout << "[Test: Virtual File System (VFS) & Sandbox Security PASSED]" << std::endl;
    return 0;
}
