#pragma once

#include "core/types.hpp"
#include <functional>
#include <mutex>
#include <cstdint>

namespace nemu::core::memory {

enum class FastmemAccessType : u8 {
    Read = 0,
    Write = 1,
    Execute = 2
};

struct FastmemFaultInfo {
    uintptr_t fault_address{0};
    vaddr_t guest_address{0};
    FastmemAccessType access_type{FastmemAccessType::Read};
    bool is_fastmem{false};
};

using FastmemFaultCallback = std::function<bool(const FastmemFaultInfo&)>;

struct FastmemStats {
    uint64_t total_faults{0};
    uint64_t fastmem_faults{0};
    uint64_t recovered_faults{0};
};

/// High-performance Vectored Exception Handler (Windows / Xbox PE32+) and
/// POSIX signal handler (Linux) intercepting memory faults within the guest's
/// 4GB/6GB/8GB fastmem virtual address reservation.
class FastmemExceptionHandler {
public:
    static FastmemExceptionHandler& Instance();

    /// Register OS exception handler (AddVectoredExceptionHandler on Win32, sigaction on Linux)
    bool Register();

    /// Unregister OS exception handler
    void Unregister();

    [[nodiscard]] bool IsRegistered() const noexcept { return is_registered_; }

    /// Register a callback invoked when a fastmem access violation occurs
    void SetFaultCallback(FastmemFaultCallback callback);
    void ClearFaultCallback();

    [[nodiscard]] FastmemStats GetStats() const noexcept;
    void ResetStats() noexcept;

    /// Called by the native VEH / signal thunk or simulator
    bool HandleFault(uintptr_t fault_addr, FastmemAccessType type, void* context_record = nullptr);

    /// Software fault simulation for test harnesses
    bool SimulateFault(uintptr_t fault_addr, FastmemAccessType type);

private:
    FastmemExceptionHandler();
    ~FastmemExceptionHandler();

    FastmemExceptionHandler(const FastmemExceptionHandler&) = delete;
    FastmemExceptionHandler& operator=(const FastmemExceptionHandler&) = delete;

    FastmemFaultCallback callback_;
    mutable std::mutex mutex_;
    FastmemStats stats_{};
    bool is_registered_{false};
    void* os_handle_{nullptr};
};

} // namespace nemu::core::memory
