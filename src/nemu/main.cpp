#include "platform/logger.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/cpu/interpreter.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "core/memory/virtual_memory.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/kernel/svc.hpp"
#include "core/loader/nro.hpp"
#include "core/filesystem/vfs.hpp"
#include "core/gpu/gpu_factory.hpp"
#include "core/gpu/maxwell_3d.hpp"
#include "core/audio/audio_factory.hpp"
#include "core/hid/hid_manager.hpp"
#include "core/config/config_manager.hpp"
#include "core/save/save_manager.hpp"
#include "core/debug/crash_dump.hpp"
#include "frontend/xbox_frontend.hpp"
#include <iostream>
#include <string_view>
#include <vector>
#include <cstring>
#include <filesystem>

using namespace nemu;
using namespace nemu::core;

int main(int argc, char** argv) {
    platform::Logger::Instance().SetMinLevel(platform::LogLevel::Info);

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  NEMU: Nintendo Switch Emulator for Xbox Series S/X     ");
    NEMU_LOG_INFO("Init", "  Target: Microsoft Xbox Developer Mode (UWP Full Trust) ");
    NEMU_LOG_INFO("Init", "  Version 0.3.0 (Milestone 9 - Complete Architecture)    ");
    NEMU_LOG_INFO("Init", "=========================================================");

    // 1. Initialize Virtual File System (VFS)
    filesystem::VirtualFileSystem vfs;
    const std::filesystem::path sdmc_root = std::filesystem::current_path() / "sdmc";
    const std::filesystem::path save_root = std::filesystem::current_path() / "save";
    std::filesystem::create_directories(sdmc_root);
    std::filesystem::create_directories(save_root);

    vfs.Mount("sdmc:/", sdmc_root, false);
    vfs.Mount("save:/", save_root, false);
    NEMU_LOG_INFO("VFS", "Mounted SD Card at '{}'", sdmc_root.string());
    NEMU_LOG_INFO("VFS", "Mounted Save Data at '{}'", save_root.string());

    // 2. Initialize Configuration Subsystem
    config::ConfigManager config(vfs);
    config.Load();
    const auto& cfg = config.GetConfig();
    NEMU_LOG_INFO("Config", "Configured Render Resolution: {}x{}", cfg.render_width, cfg.render_height);
    NEMU_LOG_INFO("Config", "CPU Execution Backend: {}", cfg.cpu_backend == config::CpuBackendMode::Jit ? "JIT (Dynamic Recompiler)" : "Interpreter");

    // 3. Initialize Save Data Manager
    save::SaveManager save_manager(vfs);

    // 4. Initialize Direct3D 12 / Null GPU Backend
    auto gpu_backend = gpu::GpuFactory::CreateBackend(cfg.render_width, cfg.render_height);
    NEMU_LOG_INFO("GPU", "Graphics Engine initialized: {}", gpu_backend->GetBackendName());
    gpu::Maxwell3D maxwell(gpu_backend);

    // 5. Initialize XAudio2 / Null Audio Backend
    auto audio_backend = audio::AudioFactory::CreateBackend(48000, 2);
    if (cfg.audio_enabled) {
        audio_backend->Start();
    }
    NEMU_LOG_INFO("Audio", "Sound Engine initialized: {} (Enabled: {})",
        audio_backend->GetBackendName(), cfg.audio_enabled ? "Yes" : "No");

    // 6. Initialize Input / HID System
    hid::HidManager hid_manager;
    hid_manager.SetButtonLayout(cfg.button_layout);
    hid_manager.SetDeadzones(cfg.inner_deadzone, cfg.outer_deadzone);
    NEMU_LOG_INFO("HID", "Xbox Wireless Controller input mapper initialized (8 players supported)");

    // 7. Initialize Xbox Frontend
    frontend::XboxFrontend frontend(vfs, config);
    frontend.Render(*gpu_backend);

    // 8. Determine Executable to Run
    std::string target_nro;
    if (argc > 1) {
        target_nro = argv[1];
    } else {
        auto req = frontend.ConsumeLaunchRequest();
        if (req) {
            target_nro = *req;
        } else if (!frontend.GetLibrary().empty()) {
            target_nro = frontend.GetLibrary()[0].virtual_path;
        }
    }

    // 9. Initialize Horizon Kernel Process and Address Space
    auto process = std::make_shared<kernel::KProcess>(1, "SwitchHomebrew");
    process->SetState(kernel::ProcessState::Running);
    memory::VirtualMemory& memory = process->GetVirtualMemory();

    vaddr_t entry_point = 0;

    if (!target_nro.empty() && target_nro != "builtin:/demo.nro") {
        NEMU_LOG_INFO("Loader", "Loading homebrew executable: {}", target_nro);
        auto loaded = loader::NroLoader::LoadFromFile(target_nro, memory);
        if (loaded) {
            entry_point = loaded->entry_point;
            NEMU_LOG_INFO("Loader", "Loaded NRO successfully at entry point 0x{:016X}", entry_point);
        } else {
            NEMU_LOG_ERROR("Loader", "Failed to load NRO from '{}'. Falling back to internal demo.", target_nro);
        }
    }

    // If no external NRO loaded, synthesize valid NRO homebrew binary in memory
    if (entry_point == 0) {
        NEMU_LOG_INFO("Init", "No external NRO loaded; synthesizing valid NRO homebrew binary in memory...");

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

        // Code at 0x80:
        // 1. MOVZ X0, #0x2A (42)  -> 0xD2800540
        // 2. MOVZ X1, #0x64 (100) -> 0xD2800C81
        // 3. ADD  X2, X0, X1      -> 0x8B010002
        // 4. RET                  -> 0xD65F03C0
        const u32 code[] = {
            0xD2800540,
            0xD2800C81,
            0x8B010002,
            0xD65F03C0
        };
        std::memcpy(demo_nro.data() + 0x80, code, sizeof(code));

        const vaddr_t LOAD_ADDR = 0x0071000000ULL;
        auto loaded = loader::NroLoader::Load(demo_nro, memory, LOAD_ADDR);
        if (loaded) {
            entry_point = loaded->entry_point;
        } else {
            NEMU_LOG_FATAL("Init", "Failed to load demo NRO");
            return 1;
        }
    }

    // 10. Create Main Thread and Initialize Context
    const vaddr_t STACK_TOP = 0x0080000000ULL;
    const vaddr_t TLS_ADDR  = 0x0080100000ULL;
    auto thread = std::make_shared<kernel::KThread>(100, process, 44, entry_point, STACK_TOP, TLS_ADDR);

    cpu::CpuState& cpu = thread->GetCpuState();
    cpu.SetX(0, 0);     // X0 = thread handle / context
    cpu.SetX(1, ~0ULL); // X1 = standalone homebrew flag
    cpu.SetX(30, 0x0071000000ULL); // Return address

    // 11. Render a startup frame via GPU
    gpu_backend->BeginFrame();
    const u32 clear_pushbuffer[] = {
        (4 << 16) | 0x0368, // ClearColor: Deep Navy Blue
        0x3D800000,         // R = 0.0625f
        0x3E000000,         // G = 0.125f
        0x3E800000,         // B = 0.25f
        0x3F800000,         // A = 1.0f
        (1 << 16) | 0x036C, // ClearSurface
        0x00000001
    };
    maxwell.SubmitPushbuffer(clear_pushbuffer);
    gpu_backend->EndFrame();
    gpu_backend->Present();

    // 12. Execute CPU Instructions
    NEMU_LOG_INFO("CPU", "Executing guest homebrew thread at PC 0x{:016X} (Mode: {})...",
        cpu.pc, cfg.cpu_backend == config::CpuBackendMode::Jit ? "JIT" : "Interpreter");

    if (cfg.cpu_backend == config::CpuBackendMode::Jit) {
        cpu::jit::JitCompiler jit;
        size_t blocks_executed = 0;
        const vaddr_t exit_pc = 0x0071000000ULL;

        while (process->GetState() == kernel::ProcessState::Running && blocks_executed < 10000) {
            bool success = jit.Execute(cpu, memory);
            if (!success) {
                NEMU_LOG_WARN("CPU", "JIT could not compile block at 0x{:016X}; falling back to Interpreter", cpu.pc);
                break;
            }
            blocks_executed++;
            if (cpu.pc == exit_pc) {
                process->SetState(kernel::ProcessState::Terminated);
                break;
            }
        }
        NEMU_LOG_INFO("CPU", "JIT executed {} basic blocks. Process State: {}",
            blocks_executed,
            process->GetState() == kernel::ProcessState::Terminated ? "Terminated (Clean Exit)" : "Running");
    }

    if (process->GetState() != kernel::ProcessState::Terminated) {
        cpu::Interpreter interp(cpu, memory);
        interp.SetSvcHandler([&](cpu::CpuState& state, u32 svc_id) {
            kernel::SvcDispatcher::Dispatch(state, *process, *thread, svc_id);
        });

        size_t instructions_executed = 0;
        while (process->GetState() == kernel::ProcessState::Running && instructions_executed < 10000) {
            auto step_res = interp.Step();
            instructions_executed++;

            if (step_res == cpu::StepResult::Halted || step_res == cpu::StepResult::UndefinedInstruction) {
                if (step_res == cpu::StepResult::UndefinedInstruction) {
                    debug::CrashContext crash_ctx{
                        .process_id = process->GetPid(),
                        .process_name = process->GetName(),
                        .thread_id = thread->GetTid(),
                        .fault_address = cpu.pc,
                        .error_message = "Undefined instruction encountered during execution",
                        .cpu_state = cpu
                    };
                    debug::CrashReporter::WriteCrashReport(crash_ctx, save_root / "crashes");
                }
                NEMU_LOG_ERROR("CPU", "Execution stopped with result {}", static_cast<int>(step_res));
                break;
            }
        }
    }

    NEMU_LOG_INFO("CPU", "Final Register State:\n{}", cpu.DumpState());

    // 13. Clean Subsystem Shutdown
    if (cfg.audio_enabled) {
        audio_backend->Stop();
    }
    audio_backend->Shutdown();
    gpu_backend->Shutdown();

    NEMU_LOG_INFO("Init", "=========================================================");
    NEMU_LOG_INFO("Init", "  Nemu execution finished successfully.                  ");
    NEMU_LOG_INFO("Init", "=========================================================");
    return 0;
}
