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

    /// Query execution metrics
    [[nodiscard]] bool IsRunning() const noexcept { return is_running_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool IsPaused() const noexcept { return is_paused_.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 GetTotalInstructionsExecuted() const noexcept;
    [[nodiscard]] u64 GetCoreInstructionsExecuted(u32 core_id) const noexcept;

private:
    void WorkerLoop(u32 core_id);
    std::shared_ptr<kernel::KThread> SelectNextThread(u32 core_id);

    std::shared_ptr<kernel::KProcess> process_;
    std::shared_ptr<cpu::jit::JitCompiler> jit_;
    SvcHandlerFn svc_handler_;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> stop_requested_{false};

    std::array<std::thread, NUM_GUEST_CORES> workers_;
    std::array<std::atomic<u64>, NUM_GUEST_CORES> core_instructions_{};
    std::array<std::atomic<bool>, NUM_GUEST_CORES> core_busy_{};

    std::mutex queue_mutex_;
    std::condition_variable cv_pause_;
};

} // namespace nemu::core::system
