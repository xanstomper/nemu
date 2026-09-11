#pragma once

#include "k_auto_object.hpp"
#include "k_handle_table.hpp"
#include "core/memory/virtual_memory.hpp"
#include <string>
#include <memory>

namespace nemu::core::kernel {

enum class ProcessState {
    Created,
    Running,
    Suspended,
    Terminated
};

class KProcess final : public KAutoObject {
public:
    static constexpr vaddr_t DEFAULT_HEAP_BASE = 0x0080000000ULL;
    static constexpr vaddr_t DEFAULT_STACK_TOP = 0x0070000000ULL;
    static constexpr size_t DEFAULT_STACK_SIZE = 4 * 1024 * 1024; // 4 MiB
    static constexpr vaddr_t DEFAULT_TLS_BASE  = 0x00C0000000ULL;

    explicit KProcess(u64 pid, std::string name);
    ~KProcess() override = default;

    [[nodiscard]] std::string_view GetTypeName() const noexcept override { return "KProcess"; }

    [[nodiscard]] u64 GetPid() const noexcept { return pid_; }
    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }
    [[nodiscard]] ProcessState GetState() const noexcept { return state_; }
    [[nodiscard]] s32 GetExitCode() const noexcept { return exit_code_; }

    void SetState(ProcessState state) noexcept { state_ = state; }
    void Terminate(s32 exit_code) noexcept {
        state_ = ProcessState::Terminated;
        exit_code_ = exit_code;
    }

    [[nodiscard]] memory::VirtualMemory& GetVirtualMemory() noexcept { return memory_; }
    [[nodiscard]] const memory::VirtualMemory& GetVirtualMemory() const noexcept { return memory_; }

    [[nodiscard]] KHandleTable& GetHandleTable() noexcept { return handle_table_; }
    [[nodiscard]] const KHandleTable& GetHandleTable() const noexcept { return handle_table_; }

    // Dynamic heap management (svcSetHeapSize)
    vaddr_t SetHeapSize(size_t size);
    [[nodiscard]] vaddr_t GetHeapBase() const noexcept { return heap_base_; }
    [[nodiscard]] size_t GetHeapSize() const noexcept { return current_heap_size_; }

private:
    u64 pid_{0};
    std::string name_;
    ProcessState state_{ProcessState::Created};
    s32 exit_code_{0};

    memory::VirtualMemory memory_;
    KHandleTable handle_table_;

    vaddr_t heap_base_{DEFAULT_HEAP_BASE};
    size_t current_heap_size_{0};
    std::mutex process_mutex_;
};

} // namespace nemu::core::kernel
