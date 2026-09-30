#include "xbox_memory_governor.hpp"
#include "logger.hpp"
#include "core/memory/memory_budget.hpp"
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#endif

namespace nemu::platform {

XboxMemoryGovernor::XboxMemoryGovernor() {
    NEMU_LOG_INFO("MemoryGovernor", "Initialized Xbox Series S/X memory guard (Ceiling: 5120MB, Critical: 4096MB, Nominal: 3840MB)");
}

u64 XboxMemoryGovernor::GetCommittedBytes() const noexcept {
    if (simulated_active_.load(std::memory_order_relaxed)) {
        return simulated_bytes_.load(std::memory_order_relaxed);
    }

    u64 host_bytes = 0;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        host_bytes = static_cast<u64>(pmc.PrivateUsage);
    }
#else
    // Linux host environment: read from /proc/self/statm (pages * page_size)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        u64 size_pages = 0;
        u64 resident_pages = 0;
        if (statm >> size_pages >> resident_pages) {
            const long page_size = sysconf(_SC_PAGESIZE);
            if (page_size > 0) {
                host_bytes = resident_pages * static_cast<u64>(page_size);
            }
        }
    }
#endif
    return std::max<u64>(host_bytes, nemu::core::memory::MemoryBudget::TotalEstimated());
}

MemoryPressureLevel XboxMemoryGovernor::GetPressureLevel() const noexcept {
    return current_level_.load(std::memory_order_relaxed);
}

std::string XboxMemoryGovernor::FormatHudString() const {
    const u64 bytes = GetCommittedBytes();
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "[RAM: " << gb << "GB / 5.1GB • ";

    switch (current_level_.load(std::memory_order_relaxed)) {
        case MemoryPressureLevel::Nominal:
            ss << "NOMINAL]";
            break;
        case MemoryPressureLevel::Elevated:
            ss << "ELEVATED]";
            break;
        case MemoryPressureLevel::Critical:
            ss << "CRITICAL (TRIMMING)]";
            break;
    }
    return ss.str();
}

void XboxMemoryGovernor::RegisterTextureTrimCallback(std::function<void()> cb) {
    std::lock_guard lock(callback_mutex_);
    texture_trim_cbs_.push_back(std::move(cb));
}

void XboxMemoryGovernor::RegisterDxgiTrimCallback(std::function<void()> cb) {
    std::lock_guard lock(callback_mutex_);
    dxgi_trim_cbs_.push_back(std::move(cb));
}

void XboxMemoryGovernor::RegisterShaderDeferralCallback(std::function<void(bool defer)> cb) {
    std::lock_guard lock(callback_mutex_);
    shader_defer_cbs_.push_back(std::move(cb));
}

void XboxMemoryGovernor::RegisterResolutionScaleCallback(std::function<void(float scale)> cb) {
    std::lock_guard lock(callback_mutex_);
    resolution_scale_cbs_.push_back(std::move(cb));
}

void XboxMemoryGovernor::RecordFrameTime(float frame_time_ms) noexcept {
    const float prev = avg_frame_time_ms_.load(std::memory_order_relaxed);
    const float updated = prev * 0.9f + frame_time_ms * 0.1f;
    avg_frame_time_ms_.store(updated, std::memory_order_relaxed);
}

float XboxMemoryGovernor::GetAverageFrameTime() const noexcept {
    return avg_frame_time_ms_.load(std::memory_order_relaxed);
}

float XboxMemoryGovernor::GetDynamicResolutionScale() const noexcept {
    return dynamic_scale_.load(std::memory_order_relaxed);
}

bool XboxMemoryGovernor::EvaluateAndEnforce() {
    const u64 commit = GetCommittedBytes();
    const MemoryPressureLevel prev = current_level_.load(std::memory_order_relaxed);
    MemoryPressureLevel next = prev;

    if (commit >= kCriticalThreshold) {
        next = MemoryPressureLevel::Critical;
    } else if (commit >= kNominalThreshold) {
        if (prev == MemoryPressureLevel::Critical) {
            // Hysteresis: only leave Critical if below kHysteresisThreshold
            if (commit < kHysteresisThreshold) {
                next = MemoryPressureLevel::Elevated;
            } else {
                next = MemoryPressureLevel::Critical;
            }
        } else {
            next = MemoryPressureLevel::Elevated;
        }
    } else {
        next = MemoryPressureLevel::Nominal;
    }

    current_level_.store(next, std::memory_order_relaxed);

    // Compute dynamic resolution scaling recommendation based on memory and frame time
    float target_scale = 1.0f;
    if (next == MemoryPressureLevel::Critical) {
        target_scale = 0.75f;
    } else if (next == MemoryPressureLevel::Elevated) {
        target_scale = 0.85f;
    } else {
        const float avg_ft = avg_frame_time_ms_.load(std::memory_order_relaxed);
        if (avg_ft > 18.0f) {
            target_scale = 0.85f;
        } else {
            target_scale = 1.0f;
        }
    }

    const float prev_scale = dynamic_scale_.load(std::memory_order_relaxed);
    if (std::abs(target_scale - prev_scale) > 0.01f) {
        dynamic_scale_.store(target_scale, std::memory_order_relaxed);
        std::lock_guard lock(callback_mutex_);
        for (const auto& cb : resolution_scale_cbs_) {
            if (cb) cb(target_scale);
        }
    }

    if (next == MemoryPressureLevel::Critical) {
        NEMU_LOG_WARN("MemoryGovernor", "Xbox memory pressure CRITICAL (Commit: {} MB / 5120 MB). Executing trim routines.",
                      commit / (1024 * 1024));

        std::lock_guard lock(callback_mutex_);
        // 1. Defer background shader pipelines
        for (const auto& cb : shader_defer_cbs_) {
            if (cb) cb(true);
        }
        // 2. Purge transient texture cache entries
        for (const auto& cb : texture_trim_cbs_) {
            if (cb) cb();
        }
        // 3. Invoke DXGI Trim on Direct3D 12 device
        for (const auto& cb : dxgi_trim_cbs_) {
            if (cb) cb();
        }
        return true;
    } else if (prev == MemoryPressureLevel::Critical && next != MemoryPressureLevel::Critical) {
        NEMU_LOG_INFO("MemoryGovernor", "Xbox memory pressure relieved (Commit: {} MB). Resuming background compilation.",
                      commit / (1024 * 1024));
        std::lock_guard lock(callback_mutex_);
        for (const auto& cb : shader_defer_cbs_) {
            if (cb) cb(false);
        }
    }

    return false;
}

void XboxMemoryGovernor::SimulateCommitBytes(u64 bytes) noexcept {
    simulated_bytes_.store(bytes, std::memory_order_relaxed);
    simulated_active_.store(true, std::memory_order_relaxed);
}

void XboxMemoryGovernor::ResetSimulatedCommit() noexcept {
    simulated_active_.store(false, std::memory_order_relaxed);
    simulated_bytes_.store(0, std::memory_order_relaxed);
}

} // namespace nemu::platform
