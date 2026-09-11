#pragma once

#include "core/types.hpp"

namespace nemu::core::kernel {
class KProcess;
class KThread;
} // namespace nemu::core::kernel

namespace nemu::core::kernel::ipc {
class KClientSession;
class ServiceRegistry;

/// Execute a Horizon svcSendSyncRequest against the given client session. The
/// guest IPC command buffer is read from the thread's TLS block (+0x100), the
/// bound HLE service dispatches the request into its reply buffer, and the
/// reply is written back to guest memory. Returns the numeric result code that
/// surfaces in the guest's X0 on return (0 == success).
u32 DispatchSyncRequest(KProcess& process, KThread& thread, KClientSession& session,
                        ServiceRegistry& registry);

} // namespace nemu::core::kernel::ipc