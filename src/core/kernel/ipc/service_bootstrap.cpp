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
#include "pl_service.hpp"
#include "nifm_service.hpp"
#include "ldn_service.hpp"
#include "caps_service.hpp"
#include "bpc_service.hpp"
#include "aoc_service.hpp"
#include "apm_service.hpp"
#include "pctl_service.hpp"
#include "prepo_service.hpp"
#include "friend_service.hpp"
#include "lm_service.hpp"
#include "mii_service.hpp"
#include "nfp_service.hpp"
#include "bcat_service.hpp"
#include "ldr_ro_service.hpp"
#include "spl_service.hpp"
#include "core/network/ldn_network.hpp"

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
,
    std::shared_ptr<nemu::core::network::LdnUdpNetwork> ldn_net) {
    (void)gpu_backend;
    auto registry = std::make_shared<ServiceRegistry>();

    // Core system services (no external deps).
    registry->Register(std::make_shared<SmService>());
    registry->Register(std::make_shared<SetSysService>());
    registry->Register(std::make_shared<SetSysService>("set"));
    registry->Register(std::make_shared<SetUserService>());
    registry->Register(std::make_shared<TimeService>());
    registry->Register(std::make_shared<AccountService>());
    registry->Register(std::make_shared<HidService>());
    registry->Register(std::make_shared<AppletManagerService>("appletOE"));
    registry->Register(std::make_shared<AppletManagerService>("appletAE"));

    // Shared Font services (pl:u, pl:s)
    registry->Register(std::make_shared<PlService>("pl:u"));
    registry->Register(std::make_shared<PlService>("pl:s"));

    // Network Interface Module services (nifm:u, nifm:s, nifm:a)
    registry->Register(std::make_shared<NifmService>("nifm:u"));
    registry->Register(std::make_shared<NifmService>("nifm:s"));
    registry->Register(std::make_shared<NifmService>("nifm:a"));

    // BSD socket services (bsd:u, bsd:s)
    registry->Register(std::make_shared<BsdService>("bsd:u"));
    registry->Register(std::make_shared<BsdService>("bsd:s"));

    // LDN local-wireless multiplayer (ldn:u, ldn:m, ldn:s) backed by the
    // real UDP LAN session layer shared with the NSO screen.
    if (!ldn_net) {
        ldn_net = std::make_shared<nemu::core::network::LdnUdpNetwork>();
    }
    registry->Register(std::make_shared<LdnService>(ldn_net, "ldn:u"));
    registry->Register(std::make_shared<LdnService>(ldn_net, "ldn:m"));
    registry->Register(std::make_shared<LdnService>(ldn_net, "ldn:s"));

    // Capture & Album services (caps:u, caps:a, caps:c, caps:ss, caps:su, caps:sc)
    registry->Register(std::make_shared<CapsService>("caps:u"));
    registry->Register(std::make_shared<CapsService>("caps:a"));
    registry->Register(std::make_shared<CapsService>("caps:c"));
    registry->Register(std::make_shared<CapsService>("caps:ss"));
    registry->Register(std::make_shared<CapsService>("caps:su"));
    registry->Register(std::make_shared<CapsService>("caps:sc"));

    // Board Power Control & RTC services (bpc, bpc:r, bpc:c, bpc:b, bpc:w, bpc:ams)
    registry->Register(std::make_shared<BpcService>("bpc"));
    registry->Register(std::make_shared<BpcService>("bpc:r"));
    registry->Register(std::make_shared<BpcService>("bpc:c"));
    registry->Register(std::make_shared<BpcService>("bpc:b"));
    registry->Register(std::make_shared<BpcService>("bpc:w"));
    registry->Register(std::make_shared<BpcService>("bpc:ams"));

    // Add-On Content / DLC services (aoc:u, aoc:s)
    registry->Register(std::make_shared<AocService>("aoc:u"));
    registry->Register(std::make_shared<AocService>("aoc:s"));

    // Application Performance Management services (apm, apm:p, apm:sys)
    registry->Register(std::make_shared<ApmService>("apm"));
    registry->Register(std::make_shared<ApmService>("apm:p"));
    registry->Register(std::make_shared<ApmSysService>("apm:sys"));

    // Parental Control services (pctl, pctl:a, pctl:s, pctl:r)
    registry->Register(std::make_shared<PctlService>("pctl"));
    registry->Register(std::make_shared<PctlService>("pctl:a"));
    registry->Register(std::make_shared<PctlService>("pctl:s"));
    registry->Register(std::make_shared<PctlService>("pctl:r"));

    // Play Report / Telemetry services (prepo:u, prepo:a, prepo:m)
    registry->Register(std::make_shared<PrepoService>("prepo:u"));
    registry->Register(std::make_shared<PrepoService>("prepo:a"));
    registry->Register(std::make_shared<PrepoService>("prepo:m"));

    // Friend / Social services (friend:u, friend:v)
    registry->Register(std::make_shared<FriendService>("friend:u"));
    registry->Register(std::make_shared<FriendService>("friend:v"));

    // Log Manager services (lm, lm:m) - ported from Eden
    registry->Register(std::make_shared<LmService>("lm"));
    registry->Register(std::make_shared<LmService>("lm:m"));

    // Mii Database services (mii:u, mii:e) - ported from Eden
    registry->Register(std::make_shared<MiiService>("mii:u"));
    registry->Register(std::make_shared<MiiService>("mii:e"));

    // Near Field Proximity / Amiibo services (nfp:user, nfc:user) - ported from Eden
    registry->Register(std::make_shared<NfpService>("nfp:user"));
    registry->Register(std::make_shared<NfpService>("nfc:user"));

    // Boxcat Delivery services (bcat:u, bcat:a, bcat:m) - ported from Eden
    registry->Register(std::make_shared<BcatService>("bcat:u"));
    registry->Register(std::make_shared<BcatService>("bcat:a"));
    registry->Register(std::make_shared<BcatService>("bcat:m"));

    // Relocatable Object Loader service (ldr:ro) - ported from Eden
    registry->Register(std::make_shared<LdrRoService>("ldr:ro"));

    // Security Cryptography services (spl, spl:ssl, spl:mig, spl:fs) - ported from Eden
    registry->Register(std::make_shared<SplService>("spl"));
    registry->Register(std::make_shared<SplService>("spl:ssl"));
    registry->Register(std::make_shared<SplService>("spl:mig"));
    registry->Register(std::make_shared<SplService>("spl:fs"));

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