#include "service_bootstrap.hpp"

#include "sm_service.hpp"
#include "set_sys_service.hpp"
#include "set_u_service.hpp"
#include "time_service.hpp"
#include "acc_service.hpp"
#include "applet_service.hpp"
#include "hid_service.hpp"
#include "fsp_srv_service.hpp"
#include "audout_service.hpp"
#include "audren_service.hpp"
#include "nvdrv_service.hpp"
#include "vi_service.hpp"

#include "core/gpu/gpu_interface.hpp"
#include "core/gpu/nvhost/nvdevice.hpp"
#include "core/gpu/presentation/nvnflinger.hpp"
#include "core/audio/audio_interface.hpp"
#include "core/filesystem/vfs.hpp"
#include "platform/logger.hpp"

namespace nemu::core::kernel::ipc {

std::shared_ptr<ServiceRegistry> CreateDefaultServiceRegistry(
    std::shared_ptr<filesystem::VirtualFileSystem> vfs,
    std::shared_ptr<audio::IAudioBackend> audio_backend,
    std::shared_ptr<gpu::IGpuBackend> gpu_backend,
    std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager,
    std::shared_ptr<gpu::presentation::Nvnflinger> flinger
) {
    (void)gpu_backend;
    auto registry = std::make_shared<ServiceRegistry>();

    // Core system services (no external deps).
    registry->Register(std::make_shared<SmService>());
    registry->Register(std::make_shared<SetSysService>());
    registry->Register(std::make_shared<SetUserService>());
    registry->Register(std::make_shared<TimeService>());
    registry->Register(std::make_shared<AccountService>());
    registry->Register(std::make_shared<HidService>());
    registry->Register(std::make_shared<AppletManagerService>("appletOE"));

    // File system service.
    if (vfs) {
        registry->Register(std::make_shared<FspSrvService>(vfs));
        registry->Register(std::make_shared<FileSystemSubService>(vfs, ""));
    }

    // Audio services.
    if (audio_backend) {
        registry->Register(std::make_shared<AudoutManagerService>(audio_backend));
        registry->Register(std::make_shared<AudrenManagerService>(audio_backend));
    }

    // GPU / NVN services.
    if (device_manager) {
        registry->Register(std::make_shared<NvDrvService>("nvdrv:a", device_manager));
        registry->Register(std::make_shared<NvDrvService>("nvdrv", device_manager));
        registry->Register(std::make_shared<NvDrvService>("nvhost:a", device_manager));
    }

    // Presentation / display services.
    if (flinger) {
        registry->Register(std::make_shared<ViService>("vi:u", flinger));
        registry->Register(std::make_shared<ViService>("vi:m", flinger));
    }

    NEMU_LOG_INFO("IPC", "Bootstrapped Horizon service registry with {} services", registry->Count());
    return registry;
}

} // namespace nemu::core::kernel::ipc