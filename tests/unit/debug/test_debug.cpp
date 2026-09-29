#include "core/debug/crash_dump.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
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
    std::cout << "[Test: CrashReporter Diagnostics]" << std::endl;

    debug::CrashContext ctx;
    ctx.process_id = 101;
    ctx.process_name = "SwitchTestGame";
    ctx.thread_id = 4;
    ctx.fault_address = 0x0071001234ULL;
    ctx.error_message = "Segmentation fault reading unmapped page";
    ctx.cpu_state.pc = 0x0071001234ULL;
    ctx.cpu_state.sp = 0x0080000000ULL;
    ctx.cpu_state.SetX(0, 0xCAFEBABEULL);
    ctx.cpu_state.SetX(1, 42);

    // Test 1: Formatting contains key crash diagnostic elements
    const std::string report = debug::CrashReporter::FormatCrashReport(ctx);
    NEMU_TEST_ASSERT(report.find("NEMU CRASH DIAGNOSTIC REPORT") != std::string::npos, "Header in report");
    NEMU_TEST_ASSERT(report.find("SwitchTestGame") != std::string::npos, "Process name in report");
    NEMU_TEST_ASSERT(report.find("PID 101") != std::string::npos, "PID in report");
    NEMU_TEST_ASSERT(report.find("0071001234") != std::string::npos, "Fault address in report");
    NEMU_TEST_ASSERT(report.find("Segmentation fault") != std::string::npos, "Error message in report");
    NEMU_TEST_ASSERT(report.find("CAFEBABE") != std::string::npos, "Register value in report");
    std::cout << "  - Crash report formatting verification: PASSED" << std::endl;

    // Test 2: Writing crash report to disk
    const std::filesystem::path crash_dir = std::filesystem::current_path() / "test_crash_sandbox";
    std::error_code ec;
    std::filesystem::remove_all(crash_dir, ec);

    NEMU_TEST_ASSERT(debug::CrashReporter::WriteCrashReport(ctx, crash_dir), "Write crash report file");

    bool file_found = false;
    for (const auto& entry : std::filesystem::directory_iterator(crash_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().filename().string().rfind("crash_", 0) == 0) {
            file_found = true;
            NEMU_TEST_ASSERT(entry.file_size(ec) > 0, "Crash report file non-empty");
            break;
        }
    }
    NEMU_TEST_ASSERT(file_found, "Crash dump file found on disk");
    std::cout << "  - Crash dump file writing: PASSED" << std::endl;

    // Test 3: Breadcrumb trail (spec §23) — last SVC/IPC/GPU/shader activity
    // lands in the crash report so a per-title failure pinpoints where it died.
    {
        debug::BreadcrumbTrail::PushSvc(0x21, 0x0071001000ULL);       // SendSyncRequest
        debug::BreadcrumbTrail::PushIpc("fsp-srv", 0x0806, 0x0071001040ULL);
        debug::BreadcrumbTrail::PushGpu(0x00B2, 0xC0FFEE);            // method=arg
        debug::BreadcrumbTrail::PushShader(0xDEADBEEF12345678ULL);

        debug::CrashContext ctx2;
        ctx2.process_id = 102;
        ctx2.process_name = "BreadcrumbGame";
        ctx2.thread_id = 7;
        ctx2.fault_address = 0x00B0000000ULL;
        ctx2.error_message = "GPU fault";
        ctx2.title_id = 0x01007EF00011E000ULL;
        ctx2.trail_count = debug::BreadcrumbTrail::Snapshot(ctx2.trail.data(), ctx2.trail.size());
        NEMU_TEST_ASSERT(ctx2.trail_count >= 4, "snapshot captured pushed entries");

        const std::string rep2 = debug::CrashReporter::FormatCrashReport(ctx2);
        NEMU_TEST_ASSERT(rep2.find("LAST ACTIVITY") != std::string::npos, "trail header in report");
        NEMU_TEST_ASSERT(rep2.find("SVC 0x21") != std::string::npos, "svc breadcrumb rendered");
        NEMU_TEST_ASSERT(rep2.find("fsp-srv") != std::string::npos, "ipc service breadcrumb rendered");
        NEMU_TEST_ASSERT(rep2.find("GPU method 0xb2") != std::string::npos, "gpu breadcrumb rendered");
        NEMU_TEST_ASSERT(rep2.find("deadbeef12345678") != std::string::npos, "shader hash rendered");
        NEMU_TEST_ASSERT(rep2.find("01007ef00011e000") != std::string::npos, "title id rendered");
        std::cout << "  - Breadcrumb trail in crash reports: PASSED" << std::endl;
    }

    // Clean up
    std::filesystem::remove_all(crash_dir, ec);

    std::cout << "[Test: CrashReporter Diagnostics PASSED]" << std::endl;
    return 0;
}
