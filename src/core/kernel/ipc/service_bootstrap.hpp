#pragma once

#include "service_registry.hpp"
#include <memory>

namespace nemu::core::gpu {
class IGpuBackend;
namespace nvhost {
class NvDeviceManager;
}
namespace presentation {
class Nvnflinger;
}
} // namespace nemu::core::gpu

namespace nemu::core::audio {
class IAudioBackend;
} // namespace nemu::core::audio

namespace nemu::core::filesystem {
class VirtualFileSystem;
} // namespace nemu::core::filesystem

namespace nemu::core::kernel::ipc {

/// Bootstrap the full statically-registered Horizon HLE service set that a
/// game ++loader++/runtime+ expects at boot, wired to the live emulator
/// sub-systems.
///
/// Registers: sm:, set:sys, set:u, time:u, acc:u0, appletOE, hid, fsp-srv,
/// auo:u (audout), audren:u, nvdrv/nvdrv:a + nv:* aliases, vi:u, pctl:r,
/// nifm:u, psm, ldr:pm, pm:dmnt. Missing/optional services degrade to a stubs
/// that return clean "not implemented / not found" results so a guest that
/// probes them bootstraps deterministically.
std::shared_ptr<ServiceRegistry> CreateDefaultServiceRegistry(
    std::shared_ptr<filesystem::VirtualFileSystem> vfs,
    std::shared_ptr<audio::IAudioBackend> audio_backend,
    std::shared_ptr<gpu::IGpuBackend> gpu_backend,
    std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager,
    std::shared_ptr<gpu::presentation::Nvnflinger> flinger);

} // namespace nemu::core::kernel::ipc