#pragma once

#include "core/types.hpp"
#include "core/kernel/k_process.hpp"
#include "core/kernel/k_thread.hpp"
#include "core/cpu/jit/jit_compiler.hpp"
#include "platform/xbox_thread_affinity.hpp"
#include <thread>
#include <atomic>
#include <array>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>

namespace nemu::core::system {

using SvcHandlerFn = std::function<void(cpu::CpuState&, u32)>;

/// GuestThreadPool: Implements true 3T+1T multithreaded guest CPU execution.
/// Dispatches guest threads across 4 dedicated host worker threads pinned to
/// hardware cores via XboxThreadAffinity:
///   - Core 0 (Guest Game Core 0)      -> Xbox Core 2 (mask 0x04)
///   - Core 1 (Guest Game Core 1)      -> Xbox Core 3 (mask 0x08)
///   - Core 2 (Guest Game Core 2)      -> Xbox Core 4 (mask 0x10)
///   - Core 3 (Guest Sysmodule/Kernel)  -> Xbox Core 6 (mask 0x40)
class GuestThreadPool {
public:
    static constexpr size_t NUM_GUEST_CORES = 4;
    static constexpr size_t DEFAULT_QUANTUM_INSTRUCTIONS = 2500;

    explicit GuestThreadPool(std::shared_ptr<kernel::KProcess> process,
                             std::shared_ptr<cpu::jit::JitCompiler> jit = nullptr);
    ~GuestThreadPool();

    GuestThreadPool(const GuestThreadPool&) = delete;
    GuestThreadPool& operator=(const GuestThreadPool&) = delete;

    /// Set SVC dispatcher callback for handling guest supervisor calls.
    void SetSvcHandler(SvcHandlerFn handler);

    /// Launch the 4 host worker threads and begin concurrent execution.
    bool Start();

    /// Pause all guest core worker threads.
    void Pause();

    /// Resume guest core worker threads from paused state.
    void Resume();

    /// Stop all guest core worker threads and join.
    void Stop();

    /// Execute a single quantum on a specific core synchronously (for single-step / headless test mode).
    size_t StepCoreSynchronous(u32 core_id, size_t instruction_budget = DEFAULT_QUANTUM_INSTRUCTIONS);

    /// Env-gated diagnostic: while a quantum is running, periodically print the
    /// live guest PC of `watch_pc`'s thread. Used to reveal silent post-walk
    /// spins (no SVC/fault log) that the frame-accounting probe never reaches.
    /// Starts a short-lived sampler that stops itself after ~30s.
    void ArmQuantumWatchdog(vaddr_t watch_pc);

    /// Query execution metrics
    [[nodiscard]] bool IsRunning() const noexcept { return is_running_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool IsPaused() const noexcept { return is_paused_.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 GetTotalInstructionsExecuted() const noexcept;
    [[nodiscard]] u64 GetCoreInstructionsExecuted(u32 core_id) const noexcept;

    /// True once any guest thread was stopped for repeatedly faulting at one PC
    /// (undecodable word, unmapped fetch, or a null return target). The headless
    /// boot probe reads this so a dead guest reports FAILED instead of BOOTED.
    [[nodiscard]] bool StalledOnFault() const noexcept {
        return stalled_on_fault_.load(std::memory_order_relaxed);
    }

    /// Enable recording of the last N guest PCs so a boot failure can be
    /// post-mortem'd. Off by default: the ring write is a single store, but the
    /// feature is opt-in so shipping builds pay nothing.
    void SetTraceEnabled(bool enabled) noexcept {
        trace_enabled_.store(enabled, std::memory_order_relaxed);
    }

    /// Most-recent-first snapshot of the PC ring (newest at index 0).
    [[nodiscard]] std::vector<vaddr_t> GetTraceSnapshot() const;
    void ClearTrace() noexcept;

private:
    void TracePush(vaddr_t pc) noexcept;
    /// Run up to `budget` instructions for `thread` on this core, counting
    /// consecutive CPU faults. A fault (undefined opcode / bad fetch) leaves PC
    /// untouched, so re-stepping the same PC spins forever; after
    /// kMaxConsecutiveFaults the thread is terminated and stalled_on_fault_ is
    /// set. Returns the number of instructions actually attempted.
    size_t RunQuantum(kernel::KThread& thread, size_t budget, vaddr_t exit_addr);

    void WorkerLoop(u32 core_id);
    std::shared_ptr<kernel::KThread> SelectNextThread(u32 core_id);

    std::shared_ptr<kernel::KProcess> process_;
    std::shared_ptr<cpu::jit::JitCompiler> jit_;
    SvcHandlerFn svc_handler_;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> stalled_on_fault_{false};

    std::array<std::thread, NUM_GUEST_CORES> workers_;
    std::array<std::atomic<u64>, NUM_GUEST_CORES> core_instructions_{};
    std::array<std::atomic<bool>, NUM_GUEST_CORES> core_busy_{};

    // Ring of recently executed guest PCs for post-mortem boot traces.
    // Large enough to span a whole call chain even when the tail of the trace is
    // dominated by a tight loop (Terraria's 8-instruction constructor-copy loop
    // alone churns thousands of entries before the failing `RET`).
    static constexpr size_t TRACE_CAPACITY = 16384;
    std::array<std::atomic<vaddr_t>, TRACE_CAPACITY> trace_pcs_{};
    std::atomic<size_t> trace_head_{0};
    std::atomic<bool> trace_enabled_{false};

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_pause_;
};

} // namespace nemu::core::system
