#include "xbox_thread_affinity.hpp"
#include "logger.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace nemu::platform {

u64 XboxThreadAffinity::GetAffinityMask(XboxThreadRole role) noexcept {
    switch (role) {
        case XboxThreadRole::OsAndUi:
            // Cores 0-1 (bits 0, 1) = 0x03
            return 0x03ULL;
        case XboxThreadRole::CpuEmulator:
            // Cores 2-3 (bits 2, 3) = 0x0C
            return 0x0CULL;
        case XboxThreadRole::GpuCommandEngine:
            // Cores 4-5 (bits 4, 5) = 0x30
            return 0x30ULL;
        case XboxThreadRole::AudioAndAuxiliary:
            // Cores 6-7 (bits 6, 7) = 0xC0
            return 0xC0ULL;
    }
    return 0xFFULL;
}

u32 XboxThreadAffinity::GetHostCoreCount() noexcept {
    const unsigned int n = std::thread::hardware_concurrency();
    return (n > 0) ? n : 1;
}

bool XboxThreadAffinity::PinCurrentThread(XboxThreadRole role, std::string_view thread_name) {
    const u64 mask = GetAffinityMask(role);
    const u32 cores = GetHostCoreCount();

#if defined(_WIN32)
    HANDLE hThread = GetCurrentThread();
    DWORD_PTR prev = SetThreadAffinityMask(hThread, static_cast<DWORD_PTR>(mask));
    if (prev != 0) {
        NEMU_LOG_INFO("ThreadAffinity", "Pinned thread '{}' to Xbox affinity mask 0x{:X}",
                      thread_name.empty() ? "unnamed" : thread_name, mask);
        return true;
    }
    return false;
#else
    if (cores >= 8) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < 8; ++i) {
            if ((mask >> i) & 1) {
                CPU_SET(i, &cpuset);
            }
        }
        int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (rc == 0) {
            NEMU_LOG_INFO("ThreadAffinity", "Pinned thread '{}' to Linux/Xbox affinity mask 0x{:X}",
                          thread_name.empty() ? "unnamed" : thread_name, mask);
            return true;
        }
    }
    return true; // Graceful non-fatal fallback on dev machines with fewer cores
#endif
}

} // namespace nemu::platform
