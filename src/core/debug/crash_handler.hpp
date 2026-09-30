#pragma once

#include <functional>
#include <string>
#include <filesystem>

namespace nemu::core::debug {

/// Installs the on-console crash handler: registers POSIX signal handlers
/// (SIGSEGV/SIGABRT/SIGFPE/SIGILL) and, on Windows, a vectored SEH exception
/// filter, so that a crash writes a CrashReporter diagnostic to disk instead of
/// dying silently. This is how a crash on the Xbox is made visible afterward —
/// the report lands in a console-accessible directory (the AppX's LOCAL:/crash/
/// by default) that you retrieve via the Device Portal, and it carries the
/// guest breadcrumb trail ("LAST ACTIVITY") pinpointing the failing
/// SVC/IPC/GPU/shader path.
///
/// Register once at startup. Idempotent; does NOT re-raise after writing so a
/// double-fault loop is avoided during the synchronous, best-effort write.
namespace CrashHandler {

/// Output directory default (relative, resolves to the process cwd — on Xbox
/// this is the AppX working dir mounted as LOCAL:/).
constexpr const char* kDefaultCrashDir = "crash";

/// Install the crash handlers. `output_dir` is where crash_*.txt files go.
void Install(const std::filesystem::path& output_dir = kDefaultCrashDir);

/// Pointer to the current guest process name/title-id provider, so reports can
/// name the failing title. Optional; may be null (report omits title info).
/// Signature: void(out_title_id, out_process_name).
using TitleProvider = std::function<void(unsigned long long&, std::string&)>;
void SetTitleProvider(TitleProvider provider);

} // namespace CrashHandler

} // namespace nemu::core::debug