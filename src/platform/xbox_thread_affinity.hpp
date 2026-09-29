#pragma once

#include "core/types.hpp"
#include <string_view>
#include <thread>

namespace nemu::platform {

enum class XboxThreadRole : u32 {
    OsAndUi = 0,             ///< UI thread & XAML compositor (Cores 0-1, mask 0x03)
    CpuEmulator = 1,         ///< Guest ARM64 execution / JIT thread (Cores 2-3, mask 0x0C)
    GpuCommandEngine = 2,    ///< Maxwell 3D & D3D12 command submission (Cores 4-5, mask 0x30)
    AudioAndAuxiliary = 3,   ///< AudioRenderer & background disk I/O (Cores 6-7, mask 0xC0)
    GuestCpuCore0 = 4,       ///< Tegra Guest CPU Core 0 (Xbox Core 2, mask 0x04)
    GuestCpuCore1 = 5,       ///< Tegra Guest CPU Core 1 (Xbox Core 3, mask 0x08)
    GuestCpuCore2 = 6,       ///< Tegra Guest CPU Core 2 (Xbox Core 4, mask 0x10)
    GuestKernelSysmodule = 7 ///< Tegra Guest Core 3 / Sysmodule (Xbox Core 6, mask 0x40)
};

class XboxThreadAffinity {
public:
    /// Returns the recommended 6-core Xbox Developer Mode affinity mask for a given subsystem role.
    [[nodiscard]] static u64 GetAffinityMask(XboxThreadRole role) noexcept;

    /// Returns the recommended Xbox Developer Mode affinity mask for a guest Tegra core ID (0-3).
    [[nodiscard]] static u64 GetGuestCoreAffinityMask(u32 guest_core_id) noexcept;

    /// Apply the recommended core affinity mask to the calling thread.
    static bool PinCurrentThread(XboxThreadRole role, std::string_view thread_name = "");

    /// Total hardware execution cores detected on current host platform.
    [[nodiscard]] static u32 GetHostCoreCount() noexcept;
};

} // namespace nemu::platform
