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
#include "core/cpu/interpreter.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <atomic>

namespace nemu::core::system {

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

    /// Step a single video frame quantum (~16.6ms of CPU, GPU, Audio, and Input work)
    bool StepFrame();

    /// Run the emulation loop until terminated or stopped
    void Run(u64 max_frames = 0);

    [[nodiscard]] EmulatorState GetState() const noexcept { return state_; }
    [[nodiscard]] u64 GetFrameCount() const noexcept { return frame_count_; }
    [[nodiscard]] u64 GetTotalInstructions() const noexcept { return total_instructions_; }

    [[nodiscard]] const std::shared_ptr<filesystem::VirtualFileSystem>& GetVfs() const noexcept { return vfs_; }
    [[nodiscard]] const std::shared_ptr<config::ConfigManager>& GetConfigManager() const noexcept { return config_manager_; }
    [[nodiscard]] const std::shared_ptr<gpu::IGpuBackend>& GetGpuBackend() const noexcept { return gpu_backend_; }
    [[nodiscard]] const std::shared_ptr<gpu::Maxwell3D>& GetMaxwell3D() const noexcept { return maxwell_; }
    [[nodiscard]] const std::shared_ptr<gpu::presentation::Nvnflinger>& GetNvnflinger() const noexcept { return flinger_; }
    [[nodiscard]] const std::shared_ptr<audio::IAudioBackend>& GetAudioBackend() const noexcept { return audio_backend_; }
    [[nodiscard]] const std::shared_ptr<hid::HidManager>& GetHidManager() const noexcept { return hid_manager_; }
    [[nodiscard]] const std::shared_ptr<hid::XboxControllerDriver>& GetControllerDriver() const noexcept { return controller_driver_; }
    [[nodiscard]] const std::shared_ptr<kernel::KProcess>& GetProcess() const noexcept { return process_; }
    [[nodiscard]] const std::shared_ptr<kernel::KThread>& GetMainThread() const noexcept { return main_thread_; }

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

    std::shared_ptr<audio::IAudioBackend> audio_backend_;
    std::shared_ptr<hid::HidManager> hid_manager_;
    std::shared_ptr<hid::XboxControllerDriver> controller_driver_;

    std::shared_ptr<kernel::KProcess> process_;
    std::shared_ptr<kernel::KThread> main_thread_;
    std::shared_ptr<kernel::ipc::ServiceRegistry> service_registry_;
    std::shared_ptr<kernel::ipc::HidService> hid_service_;

    std::unique_ptr<cpu::jit::JitCompiler> jit_;
    bool is_nro_{true};
};

} // namespace nemu::core::system
