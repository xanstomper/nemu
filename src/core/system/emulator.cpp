#include "emulator.hpp"
#include "core/loader/nro.hpp"
#include "core/gpu/gpu_factory.hpp"
#include "core/audio/audio_factory.hpp"
#include "core/kernel/svc.hpp"
#include "core/kernel/ipc/service_bootstrap.hpp"
#include "platform/logger.hpp"
#include <filesystem>
#include <thread>
#include <chrono>

namespace nemu::core::system {

namespace {
constexpr vaddr_t STACK_TOP = 0x0080000000ULL;
constexpr vaddr_t TLS_ADDR  = 0x0080100000ULL;
constexpr vaddr_t EXIT_ADDR = 0x0070000000ULL;
}

Emulator::Emulator(const EmulatorConfig& config)
    : config_(config) {
}

Emulator::~Emulator() {
    Shutdown();
}

bool Emulator::Initialize() {
    NEMU_LOG_INFO("System", "Initializing Nemu Emulator System Runtime...");

    // 1. Virtual File System
    vfs_ = std::make_shared<filesystem::VirtualFileSystem>();
    const std::filesystem::path sdmc_p = config_.sdmc_root;
    const std::filesystem::path save_p = config_.save_root;
    std::filesystem::create_directories(sdmc_p);
    std::filesystem::create_directories(save_p);

    vfs_->Mount("sdmc:/", sdmc_p, false);
    vfs_->Mount("save:/", save_p, false);

    // 2. Configuration Manager
    config_manager_ = std::make_shared<config::ConfigManager>(*vfs_);
    config_manager_->Load();

    // 3. Save Data Manager
    save_manager_ = std::make_shared<save::SaveManager>(*vfs_);

    // 4. GPU Backend & Maxwell 3D
    gpu_backend_ = gpu::GpuFactory::CreateBackend(config_.render_width, config_.render_height);
    maxwell_ = std::make_shared<gpu::Maxwell3D>(gpu_backend_);

    // 5. Audio Backend
    audio_backend_ = audio::AudioFactory::CreateBackend(48000, 2);
    if (config_.audio_enabled) {
        audio_backend_->Start();
    }

    // 6. HID Manager & Controller Driver
    hid_manager_ = std::make_shared<hid::HidManager>();
    controller_driver_ = std::make_shared<hid::XboxControllerDriver>();

    // 7. Cryptographic Keystore
    key_store_ = std::make_shared<crypto::KeyStore>();
    key_store_->LoadDefaultKeys();

    // 8. Title Loader
    title_loader_ = std::make_shared<loader::TitleLoader>(*key_store_, *vfs_);

    // 9. Horizon Process & Virtual Memory
    process_ = std::make_shared<kernel::KProcess>(1, "SwitchProcess");
    process_->SetState(kernel::ProcessState::Running);
    maxwell_->SetMemory(&process_->GetVirtualMemory());

    // 10. GPU Device Manager & Display Compositor
    device_manager_ = std::make_shared<gpu::nvhost::NvDeviceManager>(maxwell_, &process_->GetVirtualMemory());
    flinger_ = std::make_shared<gpu::presentation::Nvnflinger>(gpu_backend_);

    // 11. Horizon IPC Service Registry Bootstrap
    service_registry_ = kernel::ipc::CreateDefaultServiceRegistry(
        vfs_, audio_backend_, gpu_backend_, device_manager_, flinger_
    );
    kernel::SvcDispatcher::InitializeIpc(service_registry_);
    auto hid_base = service_registry_->Find("hid");
    if (hid_base) {
        hid_service_ = std::dynamic_pointer_cast<kernel::ipc::HidService>(hid_base);
    }

    // 12. JIT Compiler
    if (config_.jit_enabled) {
        jit_ = std::make_unique<cpu::jit::JitCompiler>();
        cpu::jit::JitCompiler::SetSvcHandler([this](cpu::CpuState& state, u32 svc_id) {
            if (process_ && main_thread_) {
                kernel::SvcDispatcher::Dispatch(state, *process_, *main_thread_, svc_id);
            }
        });
    }

    state_ = EmulatorState::Ready;
    NEMU_LOG_INFO("System", "Nemu System Runtime initialized successfully (GPU: {}, Audio: {})",
                  gpu_backend_->GetBackendName(), audio_backend_->GetBackendName());
    return true;
}

void Emulator::Shutdown() {
    if (state_ == EmulatorState::Uninitialized) return;

    state_ = EmulatorState::Terminated;
    if (audio_backend_) {
        audio_backend_->Stop();
        audio_backend_->Shutdown();
    }
    if (gpu_backend_) {
        gpu_backend_->Shutdown();
    }
    NEMU_LOG_INFO("System", "Nemu System Runtime shutdown complete");
}

bool Emulator::LoadTitle(const std::string& path) {
    if (state_ == EmulatorState::Uninitialized) {
        if (!Initialize()) return false;
    }

    NEMU_LOG_INFO("System", "Loading title into runtime: {}", path);
    auto loaded = title_loader_->LoadTitle(path, process_->GetVirtualMemory());
    if (!loaded) {
        NEMU_LOG_ERROR("System", "Failed to load title from: {}", path);
        return false;
    }

    is_nro_ = loaded->is_nro;
    if (!loaded->title_name.empty()) {
        process_->SetName(loaded->title_name);
    }

    // Create Main Thread
    main_thread_ = std::make_shared<kernel::KThread>(
        100, process_, 44, loaded->entry_point, STACK_TOP, TLS_ADDR
    );
    main_thread_->SetState(kernel::ThreadState::Ready);
    process_->AddThread(main_thread_);

    cpu::CpuState& cpu = main_thread_->GetCpuState();
    cpu.SetX(0, 0);
    cpu.SetX(1, is_nro_ ? ~0ULL : 0);
    cpu.SetX(30, EXIT_ADDR);

    // Render initial boot clear frame
    if (gpu_backend_) {
        gpu_backend_->BeginFrame();
        const u32 clear_pb[] = {
            (4 << 16) | gpu::MaxwellMethod::ClearColorR,
            0x3D800000, 0x3E000000, 0x3E800000, 0x3F800000,
            (1 << 16) | gpu::MaxwellMethod::ClearSurface,
            0x00000001
        };
        maxwell_->SubmitPushbuffer(clear_pb);
        gpu_backend_->EndFrame();
        gpu_backend_->Present();
    }

    state_ = EmulatorState::Ready;
    NEMU_LOG_INFO("System", "Title '{}' ready for execution at entry 0x{:016X}",
                  process_->GetName(), loaded->entry_point);
    return true;
}

bool Emulator::LoadBuiltinDemo() {
    if (state_ == EmulatorState::Uninitialized) {
        if (!Initialize()) return false;
    }

    NEMU_LOG_INFO("System", "Synthesizing and loading built-in demo NRO...");
    constexpr size_t TEXT_SIZE = 0x1000;
    constexpr size_t RODATA_SIZE = 0x1000;
    constexpr size_t DATA_SIZE = 0x1000;
    std::vector<u8> demo_nro(TEXT_SIZE + RODATA_SIZE + DATA_SIZE, 0);

    const u32 branch_to_code = 0x14000020; // B +0x80
    std::memcpy(demo_nro.data(), &branch_to_code, sizeof(branch_to_code));

    auto* hdr = reinterpret_cast<loader::NroHeader*>(demo_nro.data());
    hdr->entry_point_instruction = branch_to_code;
    hdr->magic = loader::NroLoader::NRO_MAGIC;
    hdr->size = static_cast<u32>(demo_nro.size());
    hdr->text.file_offset = 0;
    hdr->text.size = static_cast<u32>(TEXT_SIZE);
    hdr->rodata.file_offset = static_cast<u32>(TEXT_SIZE);
    hdr->rodata.size = static_cast<u32>(RODATA_SIZE);
    hdr->data.file_offset = static_cast<u32>(TEXT_SIZE + RODATA_SIZE);
    hdr->data.size = static_cast<u32>(DATA_SIZE);
    hdr->bss_size = 0x1000;

    const u32 code[] = {
        0xD2800540, // MOVZ X0, #42
        0xD2800C81, // MOVZ X1, #100
        0x8B010002, // ADD  X2, X0, X1
        0x91000442, // ADD  X2, X2, #1 (loop: increment X2)
        0x17FFFFFF  // B    -4         (branch to loop)
    };
    std::memcpy(demo_nro.data() + 0x80, code, sizeof(code));

    auto loaded = loader::NroLoader::Load(demo_nro, process_->GetVirtualMemory(), 0x0071000000ULL);
    if (!loaded) {
        NEMU_LOG_FATAL("System", "Failed to load built-in demo NRO");
        return false;
    }

    is_nro_ = true;
    process_->SetName("BuiltinDemo");

    main_thread_ = std::make_shared<kernel::KThread>(
        100, process_, 44, loaded->entry_point, STACK_TOP, TLS_ADDR
    );
    main_thread_->SetState(kernel::ThreadState::Ready);
    process_->AddThread(main_thread_);

    cpu::CpuState& cpu = main_thread_->GetCpuState();
    cpu.SetX(0, 0);
    cpu.SetX(1, ~0ULL);
    cpu.SetX(30, EXIT_ADDR);

    state_ = EmulatorState::Ready;
    return true;
}

void Emulator::Start() {
    if (state_ == EmulatorState::Ready || state_ == EmulatorState::Paused) {
        state_ = EmulatorState::Running;
    }
}

void Emulator::Pause() {
    if (state_ == EmulatorState::Running) {
        state_ = EmulatorState::Paused;
    }
}

void Emulator::Resume() {
    Start();
}

void Emulator::Stop() {
    state_ = EmulatorState::Stopped;
}

void Emulator::PollInput() {
    if (!controller_driver_ || !hid_manager_) return;

    for (size_t p = 0; p < hid::XboxControllerDriver::MAX_XBOX_CONTROLLERS; ++p) {
        auto state = controller_driver_->Poll(p);
        if (state) {
            hid_manager_->UpdateController(p, *state);
            if (p == 0 && hid_service_ && process_) {
                u32 btn_mask = 0;
                if (state->a) btn_mask |= (1u << 0);
                if (state->b) btn_mask |= (1u << 1);
                if (state->x) btn_mask |= (1u << 2);
                if (state->y) btn_mask |= (1u << 3);
                if (state->lb) btn_mask |= (1u << 6);
                if (state->rb) btn_mask |= (1u << 7);
                if (state->start) btn_mask |= (1u << 9);
                if (state->back) btn_mask |= (1u << 8);
                if (state->dpad_left) btn_mask |= (1u << 12);
                if (state->dpad_up) btn_mask |= (1u << 13);
                if (state->dpad_right) btn_mask |= (1u << 14);
                if (state->dpad_down) btn_mask |= (1u << 15);

                hid_service_->UpdatePadState(
                    process_->GetVirtualMemory(),
                    btn_mask,
                    static_cast<s16>(state->thumb_lx),
                    static_cast<s16>(state->thumb_ly),
                    static_cast<s16>(state->thumb_rx),
                    static_cast<s16>(state->thumb_ry)
                );
            }
        }
    }
}

void Emulator::StepCpuQuantum(size_t instruction_budget) {
    if (!process_ || process_->GetState() != kernel::ProcessState::Running) {
        state_ = EmulatorState::Terminated;
        return;
    }

    auto threads = process_->GetThreads();
    if (threads.empty()) {
        state_ = EmulatorState::Terminated;
        return;
    }

    for (const auto& thread : threads) {
        if (!thread || thread->GetState() != kernel::ThreadState::Ready) {
            continue;
        }

        cpu::CpuState& cpu = thread->GetCpuState();
        size_t executed_in_quantum = 0;

        if (jit_ && config_.jit_enabled) {
            while (executed_in_quantum < instruction_budget &&
                   process_->GetState() == kernel::ProcessState::Running &&
                   thread->GetState() == kernel::ThreadState::Ready) {
                if (cpu.pc == EXIT_ADDR || cpu.halted) {
                    thread->SetState(kernel::ThreadState::Terminated);
                    break;
                }
                bool ok = jit_->Execute(cpu, process_->GetVirtualMemory());
                if (!ok) {
                    // Fall back to interpreter for this instruction
                    cpu::Interpreter interp(cpu, process_->GetVirtualMemory());
                    interp.SetSvcHandler([this, thread](cpu::CpuState& s, u32 svc) {
                        kernel::SvcDispatcher::Dispatch(s, *process_, *thread, svc);
                    });
                    interp.Step();
                }
                executed_in_quantum += 1;
            }
        } else {
            cpu::Interpreter interp(cpu, process_->GetVirtualMemory());
            interp.SetSvcHandler([this, thread](cpu::CpuState& s, u32 svc) {
                kernel::SvcDispatcher::Dispatch(s, *process_, *thread, svc);
            });
            while (executed_in_quantum < instruction_budget &&
                   process_->GetState() == kernel::ProcessState::Running &&
                   thread->GetState() == kernel::ThreadState::Ready) {
                if (cpu.pc == EXIT_ADDR || cpu.halted) {
                    thread->SetState(kernel::ThreadState::Terminated);
                    break;
                }
                auto step_res = interp.Step();
                if (step_res == cpu::StepResult::Halted) {
                    thread->SetState(kernel::ThreadState::Terminated);
                    break;
                }
                executed_in_quantum += 1;
            }
        }

        total_instructions_ += executed_in_quantum;
    }

    // If main thread terminated, mark process terminated
    if (main_thread_ && main_thread_->GetState() == kernel::ThreadState::Terminated) {
        process_->SetState(kernel::ProcessState::Terminated);
        state_ = EmulatorState::Terminated;
    }
}

bool Emulator::StepFrame() {
    if (state_ != EmulatorState::Running && state_ != EmulatorState::Ready) {
        return false;
    }

    // 1. Poll Xbox Gamepads & update guest shared memory
    PollInput();

    // 2. Step CPU threads for one frame slice
    StepCpuQuantum(20000);

    // 3. Compose and present GPU frame
    bool presented = false;
    if (flinger_) {
        presented = flinger_->ComposeAndPresent();
    }

    if (!presented && gpu_backend_) {
        gpu_backend_->BeginFrame();
        gpu_backend_->EndFrame();
        gpu_backend_->Present();
    }

    ++frame_count_;

    if (process_ && process_->GetState() == kernel::ProcessState::Terminated) {
        state_ = EmulatorState::Terminated;
        return false;
    }

    return true;
}

void Emulator::Run(u64 max_frames) {
    Start();

    using clock = std::chrono::steady_clock;
    constexpr auto target_frame_time = std::chrono::microseconds(16666); // ~60 FPS

    while (state_ == EmulatorState::Running) {
        const auto frame_start = clock::now();

        if (!StepFrame()) {
            break;
        }

        if (max_frames > 0 && frame_count_ >= max_frames) {
            break;
        }

        if (config_.vsync) {
            const auto elapsed = clock::now() - frame_start;
            if (elapsed < target_frame_time) {
                std::this_thread::sleep_for(target_frame_time - elapsed);
            }
        }
    }

    NEMU_LOG_INFO("System", "Emulation execution loop concluded at frame {} ({} total instructions)",
                  frame_count_, total_instructions_);
}

} // namespace nemu::core::system
