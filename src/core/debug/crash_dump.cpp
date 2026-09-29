#include "crash_dump.hpp"
#include "platform/logger.hpp"
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace nemu::core::debug {

std::string CrashReporter::FormatCrashReport(const CrashContext& context) {
    std::ostringstream ss;
    ss << "=================================================================\n";
    ss << "               NEMU CRASH DIAGNOSTIC REPORT                     \n";
    ss << "=================================================================\n";

    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    ss << "Timestamp:       " << std::ctime(&time_t_now);

#ifdef _WIN32
    ss << "Host Platform:   Windows / Xbox Series S/X Developer Mode (x86-64)\n";
#else
    ss << "Host Platform:   Linux Host Environment (x86-64)\n";
#endif

    ss << "Fault Reason:    " << context.error_message << "\n";
    ss << "Fault Address:   0x" << std::hex << std::setw(16) << std::setfill('0') << context.fault_address << std::dec << "\n";
    if (context.title_id != 0) {
        ss << "Title ID:        0x" << std::hex << std::setw(16) << std::setfill('0') << context.title_id << std::dec << "\n";
    }
    ss << "Process:         PID " << context.process_id << " (" << context.process_name << ")\n";
    ss << "Thread:          TID " << context.thread_id << "\n";
    ss << "-----------------------------------------------------------------\n";
    ss << "GUEST CPU REGISTERS:\n";
    ss << context.cpu_state.DumpState();

    // Breadcrumb trail (spec §23): exactly where execution stopped.
    if (context.trail_count > 0) {
        ss << "-----------------------------------------------------------------\n";
        ss << "LAST ACTIVITY (oldest -> newest):\n";
        for (size_t i = 0; i < context.trail_count; ++i) {
            const auto& bc = context.trail[i];
            switch (bc.kind) {
            case Breadcrumb::Kind::Svc:
                ss << "  [" << i << "] SVC 0x" << std::hex << bc.a << std::dec
                   << " @ pc=0x" << std::hex << bc.pc << std::dec << "\n";
                break;
            case Breadcrumb::Kind::Ipc:
                ss << "  [" << i << "] IPC " << bc.detail << " cmd=0x" << std::hex << bc.a
                   << std::dec << " @ pc=0x" << std::hex << bc.pc << std::dec << "\n";
                break;
            case Breadcrumb::Kind::Gpu:
                ss << "  [" << i << "] GPU method 0x" << std::hex << bc.a << " = 0x" << bc.b << std::dec << "\n";
                break;
            case Breadcrumb::Kind::Shader:
                ss << "  [" << i << "] SHADER hash 0x" << std::hex
                   << ((static_cast<u64>(bc.b) << 32) | bc.a) << std::dec << "\n";
                break;
            default: break;
            }
        }
    }
    ss << "=================================================================\n";

    return ss.str();
}

bool CrashReporter::WriteCrashReport(
    const CrashContext& context,
    const std::filesystem::path& output_dir) {

    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        NEMU_LOG_ERROR("Crash", "Failed to create crash directory: {}", ec.message());
        return false;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const std::string filename = "crash_" + std::to_string(millis) + ".txt";
    const auto file_path = output_dir / filename;

    std::ofstream out(file_path);
    if (!out.is_open()) {
        NEMU_LOG_ERROR("Crash", "Failed to write crash report to {}", file_path.string());
        return false;
    }

    const std::string report = FormatCrashReport(context);
    out << report;
    out.close();

    NEMU_LOG_INFO("Crash", "Crash report written to: {}", file_path.string());
    return true;
}

} // namespace nemu::core::debug
