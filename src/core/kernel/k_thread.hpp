#pragma once

#include "k_auto_object.hpp"
#include "core/cpu/cpu_state.hpp"
#include <memory>
#include <string>

namespace nemu::core::kernel {

class KProcess;

enum class ThreadState {
    Initialized,
    Ready,
    Running,
    Waiting,
    Terminated
};

class KThread final : public KAutoObject {
public:
    KThread(u64 tid, std::shared_ptr<KProcess> owner_process, u32 priority, vaddr_t entry_point, vaddr_t stack_top, vaddr_t tls_address);
    ~KThread() override = default;

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KThread"; }

    [[nodiscard]] u64 GetTid() const noexcept { return tid_; }
    [[nodiscard]] std::shared_ptr<KProcess> GetOwnerProcess() const noexcept { return owner_process_.lock(); }
    [[nodiscard]] u32 GetPriority() const noexcept { return priority_; }
    [[nodiscard]] ThreadState GetState() const noexcept { return state_; }
    [[nodiscard]] vaddr_t GetTlsAddress() const noexcept { return tls_address_; }

    void SetState(ThreadState state) noexcept { state_ = state; }
    void SetPriority(u32 priority) noexcept { priority_ = priority; }

    [[nodiscard]] cpu::CpuState& GetCpuState() noexcept { return cpu_state_; }
    [[nodiscard]] const cpu::CpuState& GetCpuState() const noexcept { return cpu_state_; }

private:
    u64 tid_{0};
    std::weak_ptr<KProcess> owner_process_;
    u32 priority_{44};
    ThreadState state_{ThreadState::Initialized};
    vaddr_t tls_address_{0};
    cpu::CpuState cpu_state_{};
};

} // namespace nemu::core::kernel
