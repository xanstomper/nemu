// Test: On-console crash handler writes a crash report with the guest
// breadcrumb trail on a real fault.
//
// Approach: install the handler, push breadcrumbs, then in a forked child
// raise SIGSEGV. The handler writes crash_*.txt (with "LAST ACTIVITY" = the
// breadcrumb trail) to the output dir BEFORE the child dies. The parent waits
// for the child and asserts a report file appeared and contains the trail.
#include "core/debug/crash_handler.hpp"
#include "core/debug/crash_dump.hpp"
#include "core/debug/breadcrumbs.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define NEMU_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " (" << (msg) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace nemu::core;

static bool HasCrashReport(const std::filesystem::path& dir, std::string& out_path) {
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".txt" &&
            e.path().filename().string().rfind("crash_", 0) == 0) {
            out_path = e.path().string();
            return true;
        }
    }
    return false;
}

int main() {
    std::cout << "[Test: On-Console Crash Handler]" << std::endl;

    const std::filesystem::path dir = "/tmp/nemu_crash_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // Seed the global breadcrumb trail so the report carries "LAST ACTIVITY".
    debug::BreadcrumbTrail::PushSvc(0x26, 0xdeadbeef);
    debug::BreadcrumbTrail::PushIpc("audren:u", 0x0a, 0x1000);
    debug::BreadcrumbTrail::PushGpu(0x0340, 0x80000001);

    // Install BEFORE forking so the child inherits the handlers.
    debug::CrashHandler::Install(dir);
    debug::CrashHandler::SetTitleProvider([](unsigned long long& tid, std::string& name) {
        tid = 0x0100AABBCCDDEEFFULL;
        name = "SilksongDemo";
    });

    // Fork a child that raises SIGSEGV.
    const pid_t pid = ::fork();
    if (pid == 0) {
        // Child: trigger a real fault. Runtime-dependent so the compiler cannot
        // statically prove the dereference is null (which would trip
        // -Wnull-dereference) — a genuine SIGSEGV is what we're exercising.
        volatile char* p = reinterpret_cast<volatile char*>(reinterpret_cast<uintptr_t>(std::getenv("NEMU_CRASH_HANDLER_TEST")));
        const char c = *p; // deliberate SIGSEGV when env is unset (p == nullptr)
        std::cerr << "unreachable: " << static_cast<int>(c) << std::endl;
        std::exit(99); // not reached
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    NEMU_TEST_ASSERT(WIFSIGNALED(status), "child must die by signal (SIGSEGV)");

    // The handler should have written a crash report before the child died.
    std::string report_path;
    NEMU_TEST_ASSERT(HasCrashReport(dir, report_path), "a crash_*.txt report must be written");

    std::ifstream in(report_path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // The report must carry the fault reason and the breadcrumb "LAST ACTIVITY".
    NEMU_TEST_ASSERT(content.find("CRASH DIAGNOSTIC") != std::string::npos, "report header present");
    NEMU_TEST_ASSERT(content.find("SIGSEGV") != std::string::npos, "fault reason present");
    NEMU_TEST_ASSERT(content.find("LAST ACTIVITY") != std::string::npos, "breadcrumb trail present");
    NEMU_TEST_ASSERT(content.find("audren:u") != std::string::npos, "IPC breadcrumb present");
    NEMU_TEST_ASSERT(content.find("SilksongDemo") != std::string::npos, "title name present");

    std::cout << "  Report written: " << report_path << "\n";
    std::cout << "  (contains fault reason + LAST ACTIVITY trail)\n";
    std::cout << "[Test: On-Console Crash Handler] ALL PASSED" << std::endl;
    return 0;
}