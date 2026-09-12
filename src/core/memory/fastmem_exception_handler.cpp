#include "fastmem_exception_handler.hpp"
#include "fastmem.hpp"
#include "platform/logger.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS ep) {
    if (!ep || !ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        nemu::core::memory::FastmemAccessType type = nemu::core::memory::FastmemAccessType::Read;
        if (ep->ExceptionRecord->ExceptionInformation[0] == 1) {
            type = nemu::core::memory::FastmemAccessType::Write;
        } else if (ep->ExceptionRecord->ExceptionInformation[0] == 8) {
            type = nemu::core::memory::FastmemAccessType::Execute;
        }
        const uintptr_t fault_addr = ep->ExceptionRecord->ExceptionInformation[1];
        if (nemu::core::memory::FastmemExceptionHandler::Instance().HandleFault(fault_addr, type, ep->ContextRecord)) {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
#include <signal.h>
#include <ucontext.h>

static struct sigaction s_old_sa{};
static bool s_has_old_sa = false;

static void PosixSignalHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGSEGV && info) {
        const uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
        nemu::core::memory::FastmemAccessType type = nemu::core::memory::FastmemAccessType::Read;
        auto* uc = reinterpret_cast<ucontext_t*>(ucontext);
        if (uc) {
#if defined(REG_ERR)
            if (uc->uc_mcontext.gregs[REG_ERR] & 0x2) {
                type = nemu::core::memory::FastmemAccessType::Write;
            }
#endif
        }
        if (nemu::core::memory::FastmemExceptionHandler::Instance().HandleFault(fault_addr, type, ucontext)) {
            return;
        }
    }
    if (s_has_old_sa && s_old_sa.sa_sigaction) {
        s_old_sa.sa_sigaction(sig, info, ucontext);
    } else if (s_has_old_sa && s_old_sa.sa_handler && s_old_sa.sa_handler != SIG_DFL && s_old_sa.sa_handler != SIG_IGN) {
        s_old_sa.sa_handler(sig);
    } else {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    }
}
#endif

namespace nemu::core::memory {

FastmemExceptionHandler::FastmemExceptionHandler() = default;

FastmemExceptionHandler::~FastmemExceptionHandler() {
    Unregister();
}

FastmemExceptionHandler& FastmemExceptionHandler::Instance() {
    static FastmemExceptionHandler s_instance;
    return s_instance;
}

bool FastmemExceptionHandler::Register() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_registered_) {
        return true;
    }

#if defined(_WIN32)
    // Add first-chance vectored exception handler
    os_handle_ = AddVectoredExceptionHandler(1, VectoredHandler);
    if (!os_handle_) {
        NEMU_LOG_ERROR("Fastmem", "AddVectoredExceptionHandler failed (error {})", GetLastError());
        return false;
    }
#else
    struct sigaction sa{};
    sa.sa_sigaction = PosixSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, &s_old_sa) != 0) {
        NEMU_LOG_ERROR("Fastmem", "sigaction(SIGSEGV) registration failed");
        return false;
    }
    s_has_old_sa = true;
#endif

    is_registered_ = true;
    NEMU_LOG_INFO("Fastmem", "Registered Fastmem OS exception handler (VEH / SIGSEGV)");
    return true;
}

void FastmemExceptionHandler::Unregister() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_registered_) {
        return;
    }

#if defined(_WIN32)
    if (os_handle_) {
        RemoveVectoredExceptionHandler(os_handle_);
        os_handle_ = nullptr;
    }
#else
    if (s_has_old_sa) {
        sigaction(SIGSEGV, &s_old_sa, nullptr);
        s_has_old_sa = false;
    }
#endif

    is_registered_ = false;
    NEMU_LOG_INFO("Fastmem", "Unregistered Fastmem OS exception handler");
}

void FastmemExceptionHandler::SetFaultCallback(FastmemFaultCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

void FastmemExceptionHandler::ClearFaultCallback() {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = nullptr;
}

FastmemStats FastmemExceptionHandler::GetStats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void FastmemExceptionHandler::ResetStats() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = FastmemStats{};
}

bool FastmemExceptionHandler::HandleFault(uintptr_t fault_addr, FastmemAccessType type,
                                          [[maybe_unused]] void* context_record) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.total_faults++;

    FastmemManager& fm = FastmemManager::Instance();
    if (!fm.IsEnabled()) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(fm.GetBase());
    const size_t res_size = fm.GetTotalReservationSize();

    if (fault_addr >= base && fault_addr < (base + res_size)) {
        stats_.fastmem_faults++;
        FastmemFaultInfo info{
            .fault_address = fault_addr,
            .guest_address = static_cast<vaddr_t>(fault_addr - base),
            .access_type = type,
            .is_fastmem = true
        };

        if (callback_) {
            const bool recovered = callback_(info);
            if (recovered) {
                stats_.recovered_faults++;
                return true;
            }
        }
    }

    return false;
}

bool FastmemExceptionHandler::SimulateFault(uintptr_t fault_addr, FastmemAccessType type) {
    return HandleFault(fault_addr, type, nullptr);
}

} // namespace nemu::core::memory
