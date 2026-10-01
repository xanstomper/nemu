#pragma once

#include "core/types.hpp"
#include "core/config/config_manager.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/crypto/key_store.hpp"
#include "core/loader/title_loader.hpp"
#include "core/save/save_manager.hpp"
#include "core/gpu/gpu_interface.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/gpu/presentation/nvnflinger.hpp"
#include "core/gpu/nvhost/nvdevice.hpp"
#include "core/audio/audio_interface.hpp"
#include "core/hid/hid_manager.hpp"
#include "core/hid/xbox_controller_driver.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/ipc/service_registry.hpp"
#include "core/kernel/ipc/hid_service.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/network/ldn_network.hpp"
#include "core/cpu/interpreter.hpp"
#include "guest_thread_pool.hpp"
#include "platform/xbox_memory_governor.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <atomic>

namespace nemu::core::system {

class GuestThreadPool;

enum class EmulatorState {
    Uninitialized,
    Ready,
    Running,
    Paused,
    Stopped,
    Terminated,
};

struct EmulatorConfig {
    u32 render_width{1280};
    u32 render_height{720};
    bool vsync{true};
    bool audio_enabled{true};
    bool jit_enabled{true};
    std::string sdmc_root{"./sdmc"};
    std::string save_root{"./save"};
    std::string title_path{};
    size_t texture_budget_bytes{0}; // 0 = backend default (1.5 GiB)
};

class Emulator {
public:
    explicit Emulator(const EmulatorConfig& config = {});
    ~Emulator();

    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;

    /// Initialize all hardware and OS emulation subsystems
    bool Initialize();

    /// Cleanly shutdown all subsystems
    void Shutdown();

    /// Load a Nintendo Switch title from file (.nro, .nso, .nsp, .xci, .nca)
    bool LoadTitle(const std::string& path);

    /// Load a synthesized demo NRO for baseline verification
    bool LoadBuiltinDemo();

    /// Start execution loop
    void Start();

    /// Pause execution
    void Pause();

    /// Resume from paused state
    void Resume();

    /// Request stop
    void Stop();

    /// Save emulation state to slot (0..9)
    bool SaveState(u32 slot);

    /// Load emulation state from slot (0..9)
    bool LoadState(u32 slot);

    /// Step a single video frame quantum (~16.6ms of CPU, GPU, Audio, and Input work)
    bool StepFrame();

    /// Run the emulation loop until terminated or stopped
    void Run(u64 max_frames = 0);

    [[nodiscard]] EmulatorState GetState() const noexcept { return state_; }
    [[nodiscard]] u64 GetFrameCount() const noexcept { return frame_count_; }
    [[nodiscard]] u64 GetTotalInstructions() const noexcept { return total_instructions_; }

    /// True when a guest thread was stopped for repeatedly faulting at one PC
    /// (unresolved entry point, missing opcode, or unmapped page). The headless
    /// boot probe treats this as a FAILED boot rather than success.
    /// Both execution paths are consulted: the inline interpreter/JIT fallback
    /// loop in StepCpuQuantum, and the guest thread pool (which is the path that
    /// actually runs during a normal boot, so it needs its own watchdog).
    [[nodiscard]] bool StalledOnFault() const noexcept {
        return stalled_on_fault_ ||
               (thread_pool_ && thread_pool_->StalledOnFault());
    }

    [[nodiscard]] const std::shared_ptr<filesystem::VirtualFileSystem>& GetVfs() const noexcept { return vfs_; }
    [[nodiscard]] const std::shared_ptr<config::ConfigManager>& GetConfigManager() const noexcept { return config_manager_; }
    [[nodiscard]] const std::shared_ptr<crypto::KeyStore>& GetKeyStore() const noexcept { return key_store_; }
    [[nodiscard]] const std::shared_ptr<gpu::IGpuBackend>& GetGpuBackend() const noexcept { return gpu_backend_; }
    [[nodiscard]] const std::shared_ptr<gpu::Maxwell3D>& GetMaxwell3D() const noexcept { return maxwell_; }
    [[nodiscard]] const std::shared_ptr<gpu::presentation::Nvnflinger>& GetNvnflinger() const noexcept { return flinger_; }
    [[nodiscard]] const std::shared_ptr<audio::IAudioBackend>& GetAudioBackend() const noexcept { return audio_backend_; }
    [[nodiscard]] const std::shared_ptr<hid::HidManager>& GetHidManager() const noexcept { return hid_manager_; }
    [[nodiscard]] const std::shared_ptr<hid::XboxControllerDriver>& GetControllerDriver() const noexcept { return controller_driver_; }
    [[nodiscard]] const std::shared_ptr<kernel::KProcess>& GetProcess() const noexcept { return process_; }
    [[nodiscard]] const std::shared_ptr<kernel::KThread>& GetMainThread() const noexcept { return main_thread_; }
    /// Live JIT recompiler stats for the Diagnostics screen (null when JIT disabled)
    [[nodiscard]] const cpu::jit::JitCompiler* GetJitCompiler() const noexcept { return jit_.get(); }
    [[nodiscard]] const std::shared_ptr<GuestThreadPool>& GetGuestThreadPool() const noexcept { return thread_pool_; }

    /// Recent guest PCs, newest first. Populated only when
    /// GuestThreadPool::SetTraceEnabled(true) was called; empty otherwise.
    [[nodiscard]] std::vector<vaddr_t> GetBootTrace() const {
        return thread_pool_ ? thread_pool_->GetTraceSnapshot()
                            : std::vector<vaddr_t>{};
    }

    /// Apply the persisted runtime config to live subsystems (HID layout,
    /// deadzones, vibration, audio). Called at boot and whenever the UI
    /// changes a setting so everything the Switch UI shows is what runs.
    void ApplyRuntimeConfig();

    /// Shared UDP LAN multiplayer backend (also registered as ldn:u IPC).
    [[nodiscard]] const std::shared_ptr<network::LdnUdpNetwork>& GetLdnNetwork() const noexcept { return ldn_net_; }

    /// Xbox memory governor (3-tier memory protection + dynamic resolution scaling)
    [[nodiscard]] const std::shared_ptr<platform::XboxMemoryGovernor>& GetMemoryGovernor() const noexcept { return memory_governor_; }

private:
    void PollInput();
    void StepCpuQuantum(size_t instruction_budget);

    EmulatorConfig config_{};
    std::atomic<EmulatorState> state_{EmulatorState::Uninitialized};
    u64 frame_count_{0};
    u64 total_instructions_{0};

    // Subsystems
    std::shared_ptr<filesystem::VirtualFileSystem> vfs_;
    std::shared_ptr<config::ConfigManager> config_manager_;
    std::shared_ptr<save::SaveManager> save_manager_;
    std::shared_ptr<crypto::KeyStore> key_store_;
    std::shared_ptr<loader::TitleLoader> title_loader_;

    std::shared_ptr<gpu::IGpuBackend> gpu_backend_;
    std::shared_ptr<gpu::Maxwell3D> maxwell_;
    std::shared_ptr<gpu::presentation::Nvnflinger> flinger_;
    std::shared_ptr<gpu::nvhost::NvDeviceManager> device_manager_;
    u32 current_render_width_{1280}; // tracks live backend resolution (scale changes)

    std::shared_ptr<audio::IAudioBackend> audio_backend_;
    std::shared_ptr<hid::HidManager> hid_manager_;
    std::shared_ptr<hid::XboxControllerDriver> controller_driver_;

    std::shared_ptr<kernel::KProcess> process_;
    std::shared_ptr<kernel::KThread> main_thread_;
    std::shared_ptr<kernel::ipc::ServiceRegistry> service_registry_;
    std::shared_ptr<kernel::ipc::HidService> hid_service_;

    std::unique_ptr<cpu::jit::JitCompiler> jit_;
    std::shared_ptr<GuestThreadPool> thread_pool_;
    std::shared_ptr<network::LdnUdpNetwork> ldn_net_;
    std::shared_ptr<platform::XboxMemoryGovernor> memory_governor_;
    bool is_nro_{true};
    bool stalled_on_fault_{false};  // guest thread stopped for a repeated PC fault
};

} // namespace nemu::core::system
