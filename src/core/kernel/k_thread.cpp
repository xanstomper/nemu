#include "k_thread.hpp"
#include "k_process.hpp"

namespace nemu::core::kernel {

KThread::KThread(u64 tid, std::shared_ptr<KProcess> owner_process, u32 priority, vaddr_t entry_point, vaddr_t stack_top, vaddr_t tls_address)
    : KAutoObject(HandleType::Thread),
      tid_(tid),
      owner_process_(owner_process),
      priority_(priority),
      state_(ThreadState::Initialized),
      tls_address_(tls_address) {
    cpu_state_.Reset();
    cpu_state_.pc = entry_point;
    cpu_state_.sp = stack_top;
    cpu_state_.tpidrro_el0 = tls_address;
}

} // namespace nemu::core::kernel
