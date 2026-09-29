#pragma once

#include "core/types.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/debug/breadcrumbs.hpp"
#include <string>
#include <string_view>
#include <filesystem>
#include <array>

namespace nemu::core::debug {

struct CrashContext {
    u64 process_id{0};
    std::string process_name;
    u64 thread_id{0};
    vaddr_t fault_address{0};
    std::string error_message;
    cpu::CpuState cpu_state{};
    // Last-activity breadcrumb trail (spec §23): oldest→newest.
    std::array<Breadcrumb, BreadcrumbTrail::kSlots> trail{};
    size_t trail_count{0};
    u64 title_id{0};
};

class CrashReporter {
public:
    static std::string FormatCrashReport(const CrashContext& context);

    static bool WriteCrashReport(
        const CrashContext& context,
        const std::filesystem::path& output_dir);
};

} // namespace nemu::core::debug
