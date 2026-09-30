#include "core/debug/crash_handler.hpp"
#include "core/debug/crash_dump.hpp"
#include "core/debug/breadcrumbs.hpp"
#include "platform/logger.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nemu::core::debug {
namespace {

std::atomic<bool> g_handling{false};
std::filesystem::path g_output_dir;
CrashHandler::TitleProvider g_title_provider;

/// Build the CrashContext and write the report. Called from a signal/SEH
/// context, so it must be async-signal-safe-ish: we use the breadcrumb
/// Snapshot (lock-free-ish by design) and synchronous file I/O. If a crash
/// happens inside it, g_handling prevents recursion and we terminate.
void WriteCrashReportInternal(const char* fault_reason, unsigned long long fault_addr) {
    if (g_handling.exchange(true)) {
        return; // already handling / re-entered
    }

    CrashContext ctx;
    ctx.error_message = fault_reason ? fault_reason : "unknown fault";
    ctx.fault_address = fault_addr;

    // Pull the guest title identity (process name + title id) if provided.
    if (g_title_provider) {
        std::string name;
        unsigned long long tid = 0;
        g_title_provider(tid, name);
        ctx.title_id = tid;
        ctx.process_name = std::move(name);
    }

    // The crown jewel: the last-activity trail (SVC/IPC/GPU/shader) leading up
    // to the crash — exactly what pinpoints the failing subsystem on the box.
    Breadcrumb out[BreadcrumbTrail::kSlots]{};
    const size_t n = BreadcrumbTrail::Snapshot(out, BreadcrumbTrail::kSlots);
    for (size_t i = 0; i < n && i < ctx.trail.size(); ++i) {
        ctx.trail[i] = out[i];
    }
    ctx.trail_count = n;

    std::error_code ec;
    std::filesystem::create_directories(g_output_dir, ec);
    const bool ok = CrashReporter::WriteCrashReport(ctx, g_output_dir);
    NEMU_LOG_ERROR("Crash", "Crash handler {} report to {} ({})",
                   ok ? "wrote" : "FAILED to write",
                   g_output_dir.string(), fault_reason ? fault_reason : "unknown");
}

// POSIX signal handler (Linux desktop + MinGW MSVCRT signal model).
extern "C" void HandleSignal(int sig) {
    const char* reason = "unknown signal";
    unsigned long long addr = 0;
    switch (sig) {
        case SIGSEGV: reason = "SIGSEGV (invalid memory access)"; break;
        case SIGABRT: reason = "SIGABRT (abort)"; break;
        case SIGFPE:  reason = "SIGFPE (floating point / divide error)"; break;
        case SIGILL:  reason = "SIGILL (illegal instruction)"; break;
        default:      reason = "fatal signal"; break;
    }
    WriteCrashReportInternal(reason, addr);
    // Re-raise with default disposition so the OS reports it / exits cleanly.
    std::signal(sig, SIG_DFL);
    ::raise(sig);
}

#ifdef _WIN32
// Vectored SEH filter (console async faults that don't surface as POSIX
// signals under UWP). Best-effort: we cannot safely call much, but writing the
// breadcrumb report is exactly the recoverable path.
LONG WINAPI SehFilter(PEXCEPTION_POINTERS info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const char* reason = "SEH exception";
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: reason = "ACCESS_VIOLATION (invalid memory access)"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO: reason = "INT_DIVIDE_BY_ZERO"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: reason = "ILLEGAL_INSTRUCTION"; break;
        case EXCEPTION_STACK_OVERFLOW: reason = "STACK_OVERFLOW"; break;
        default: reason = "SEH exception"; break;
    }
    unsigned long long addr = 0;
    if (info->ExceptionRecord->NumberParameters >= 2) {
        addr = info->ExceptionRecord->ExceptionInformation[1];
    }
    WriteCrashReportInternal(reason, addr);
    return EXCEPTION_CONTINUE_EXECUTION; // optimistic; report already on disk
}
#endif

} // namespace

// ---------------------------------------------------------------------------

void CrashHandler::Install(const std::filesystem::path& output_dir) {
    g_output_dir = output_dir;

#ifdef _WIN32
    AddVectoredExceptionHandler(1, SehFilter);
#endif
    std::signal(SIGSEGV, HandleSignal);
    std::signal(SIGABRT, HandleSignal);
    std::signal(SIGFPE,  HandleSignal);
    std::signal(SIGILL,  HandleSignal);
    NEMU_LOG_INFO("Crash", "Crash handler installed (reports -> {})",
                  g_output_dir.string());
}

void CrashHandler::SetTitleProvider(TitleProvider provider) {
    g_title_provider = provider;
}

} // namespace nemu::core::debug