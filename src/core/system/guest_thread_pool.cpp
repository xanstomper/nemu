#include "guest_thread_pool.hpp"
#include "core/cpu/interpreter.hpp"
#include "platform/logger.hpp"
#include <algorithm>
#include <chrono>

namespace nemu::core::system {

namespace {
constexpr vaddr_t EXIT_ADDR = 0x00000000DEAD0000ULL;
}

GuestThreadPool::GuestThreadPool(std::shared_ptr<kernel::KProcess> process,
                                 std::shared_ptr<cpu::jit::JitCompiler> jit)
    : process_(std::move(process)), jit_(std::move(jit)) {
    for (size_t i = 0; i < NUM_GUEST_CORES; ++i) {
        core_instructions_[i].store(0, std::memory_order_relaxed);
        core_busy_[i].store(false, std::memory_order_relaxed);
    }
}

GuestThreadPool::~GuestThreadPool() {
    Stop();
}

void GuestThreadPool::SetSvcHandler(SvcHandlerFn handler) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    svc_handler_ = std::move(handler);
}

bool GuestThreadPool::Start() {
    if (is_running_.load(std::memory_order_relaxed)) {
        return true;
    }

    stop_requested_.store(false, std::memory_order_release);
    is_paused_.store(false, std::memory_order_release);
    is_running_.store(true, std::memory_order_release);

    NEMU_LOG_INFO("CPU", "Starting 3T+1T Guest Thread Pool (Cores 0-2 Game, Core 3 Sysmodule)...");

    for (u32 core_id = 0; core_id < NUM_GUEST_CORES; ++core_id) {
        workers_[core_id] = std::thread(&GuestThreadPool::WorkerLoop, this, core_id);
    }

    return true;
}

void GuestThreadPool::Pause() {
    if (!is_running_.load(std::memory_order_relaxed)) return;
    is_paused_.store(true, std::memory_order_release);
}

void GuestThreadPool::Resume() {
    if (!is_running_.load(std::memory_order_relaxed)) return;
    is_paused_.store(false, std::memory_order_release);
    cv_pause_.notify_all();
}

void GuestThreadPool::Stop() {
    if (!is_running_.load(std::memory_order_relaxed)) {
        return;
    }

    stop_requested_.store(true, std::memory_order_release);
    is_paused_.store(false, std::memory_order_release);
    cv_pause_.notify_all();

    for (u32 i = 0; i < NUM_GUEST_CORES; ++i) {
        if (workers_[i].joinable()) {
            workers_[i].join();
        }
    }

    is_running_.store(false, std::memory_order_release);
    NEMU_LOG_INFO("CPU", "Guest Thread Pool gracefully stopped.");
}

std::shared_ptr<kernel::KThread> GuestThreadPool::SelectNextThread(u32 core_id) {
    if (!process_) return nullptr;

    const auto threads = process_->GetThreads();
    std::shared_ptr<kernel::KThread> best_thread = nullptr;
    u32 highest_prio = 64; // lower number = higher Horizon priority

    const u64 core_bit = 1ULL << core_id;

    for (const auto& t : threads) {
        if (!t) continue;
        if (t->GetState() != kernel::ThreadState::Ready) continue;

        // Affinity mask filter
        if ((t->GetAffinityMask() & core_bit) == 0) continue;

        // Prefer threads with matching ideal core
        const bool matches_ideal = (t->GetIdealCore() == static_cast<s32>(core_id) || t->GetIdealCore() < 0);
        const u32 prio = t->GetPriority();

        if (matches_ideal) {
            if (prio < highest_prio) {
                highest_prio = prio;
                best_thread = t;
            }
        } else if (!best_thread && prio < highest_prio) {
            highest_prio = prio;
            best_thread = t;
        }
    }

    return best_thread;
}

void GuestThreadPool::TracePush(vaddr_t pc) noexcept {
    if (!trace_enabled_.load(std::memory_order_relaxed)) {
        return;
    }
    const size_t head = trace_head_.fetch_add(1, std::memory_order_relaxed);
    trace_pcs_[head % TRACE_CAPACITY].store(pc, std::memory_order_relaxed);
}

std::vector<vaddr_t> GuestThreadPool::GetTraceSnapshot() const {
    const size_t head = trace_head_.load(std::memory_order_relaxed);
    const size_t n = std::min<size_t>(head, TRACE_CAPACITY);
    std::vector<vaddr_t> out;
    out.reserve(n);
    // Newest first.
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = (head - 1 - i);
        out.push_back(trace_pcs_[idx % TRACE_CAPACITY].load(std::memory_order_relaxed));
    }
    return out;
}

void GuestThreadPool::ClearTrace() noexcept {
    trace_head_.store(0, std::memory_order_relaxed);
}

size_t GuestThreadPool::RunQuantum(kernel::KThread& thread, size_t budget, vaddr_t exit_addr) {
    // Consecutive CPU faults at one PC. A fault leaves PC untouched, so
    // re-stepping it spins; past this many in a row the thread is genuinely
    // stuck (undecodable word / unmapped fetch / null return target) and we stop
    // it instead of burning the whole budget. This is the *only* place guest
    // execution happens when the thread pool is active, so the accounting has to
    // live here -- previously both loops below ignored StepResult entirely, which
    // let a guest spinning on one bad PC score millions of "instructions" and be
    // reported as BOOTED by the headless boot probe.
    constexpr unsigned kMaxConsecutiveFaults = 64;

    cpu::CpuState& cpu = thread.GetCpuState();
    size_t executed = 0;
    unsigned fault_streak = 0;
    vaddr_t first_fault_pc = 0;

    while (executed < budget &&
           !stop_requested_.load(std::memory_order_relaxed) &&
           !is_paused_.load(std::memory_order_relaxed) &&
           process_->GetState() == kernel::ProcessState::Running &&
           thread.GetState() == kernel::ThreadState::Running) {

        if (cpu.pc == exit_addr || cpu.halted) {
            thread.SetState(kernel::ThreadState::Terminated);
            break;
        }

        bool ok = false;
        if (jit_) {
            ok = jit_->Execute(cpu, process_->GetVirtualMemory());
        }

        cpu::StepResult step_res = cpu::StepResult::Ok;
        if (!ok) {
            cpu::Interpreter interp(cpu, process_->GetVirtualMemory());
            if (svc_handler_) {
                interp.SetSvcHandler(svc_handler_);
            }
            step_res = interp.Step();
        }

        // Record where the guest actually was, so a boot failure can be
        // post-mortem'd without re-running under a debugger.
        TracePush(cpu.pc);

        if (step_res == cpu::StepResult::Halted) {
            thread.SetState(kernel::ThreadState::Terminated);
            break;
        }

        if (step_res == cpu::StepResult::UndefinedInstruction ||
            step_res == cpu::StepResult::MemoryFault) {
            if (++fault_streak == 1) {
                first_fault_pc = cpu.pc;
            }
            if (fault_streak >= kMaxConsecutiveFaults) {
                NEMU_LOG_ERROR("CPU",
                    "Thread {} stalled at PC 0x{:016X}: {} consecutive CPU faults "
                    "(unresolved entry / missing opcode / unmapped page). "
                    "Terminating thread instead of spinning.",
                    thread.GetTid(), first_fault_pc, fault_streak);
                thread.SetState(kernel::ThreadState::Terminated);
                stalled_on_fault_.store(true, std::memory_order_relaxed);
                break;
            }
        } else {
            fault_streak = 0;
        }

        ++executed;
    }

    return executed;
}

void GuestThreadPool::WorkerLoop(u32 core_id) {
    // Pin host thread to target Xbox Developer Mode core
    platform::XboxThreadRole role = platform::XboxThreadRole::GuestCpuCore0;
    if (core_id == 1) role = platform::XboxThreadRole::GuestCpuCore1;
    else if (core_id == 2) role = platform::XboxThreadRole::GuestCpuCore2;
    else if (core_id == 3) role = platform::XboxThreadRole::GuestKernelSysmodule;

    const std::string thread_name = "NemuGuestCore" + std::to_string(core_id);
    platform::XboxThreadAffinity::PinCurrentThread(role, thread_name);

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        if (is_paused_.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_pause_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                return !is_paused_.load(std::memory_order_relaxed) ||
                       stop_requested_.load(std::memory_order_relaxed);
            });
            continue;
        }

        if (!process_ || process_->GetState() != kernel::ProcessState::Running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::shared_ptr<kernel::KThread> thread;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            thread = SelectNextThread(core_id);
            if (thread) {
                thread->SetState(kernel::ThreadState::Running);
            }
        }

        if (!thread) {
            core_busy_[core_id].store(false, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(150));
            continue;
        }

        core_busy_[core_id].store(true, std::memory_order_relaxed);
        const size_t executed = RunQuantum(*thread, DEFAULT_QUANTUM_INSTRUCTIONS, EXIT_ADDR);

        core_instructions_[core_id].fetch_add(executed, std::memory_order_relaxed);

        // If still running after quantum, return to Ready state so priority rotation occurs
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (thread->GetState() == kernel::ThreadState::Running) {
                thread->SetState(kernel::ThreadState::Ready);
            }
        }
    }

    core_busy_[core_id].store(false, std::memory_order_relaxed);
}

size_t GuestThreadPool::StepCoreSynchronous(u32 core_id, size_t instruction_budget) {
    if (!process_ || process_->GetState() != kernel::ProcessState::Running) {
        return 0;
    }

    std::shared_ptr<kernel::KThread> thread = SelectNextThread(core_id);
    if (!thread) return 0;

    thread->SetState(kernel::ThreadState::Running);
    const size_t executed = RunQuantum(*thread, instruction_budget, EXIT_ADDR);

    core_instructions_[core_id].fetch_add(executed, std::memory_order_relaxed);

    if (thread->GetState() == kernel::ThreadState::Running) {
        thread->SetState(kernel::ThreadState::Ready);
    }

    return executed;
}

u64 GuestThreadPool::GetTotalInstructionsExecuted() const noexcept {
    u64 total = 0;
    for (size_t i = 0; i < NUM_GUEST_CORES; ++i) {
        total += core_instructions_[i].load(std::memory_order_relaxed);
    }
    return total;
}

u64 GuestThreadPool::GetCoreInstructionsExecuted(u32 core_id) const noexcept {
    if (core_id >= NUM_GUEST_CORES) return 0;
    return core_instructions_[core_id].load(std::memory_order_relaxed);
}

} // namespace nemu::core::system
