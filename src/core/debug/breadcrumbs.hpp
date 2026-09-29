#pragma once

#include "core/types.hpp"
#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace nemu::core::debug {

// ---------------------------------------------------------------------------
// Breadcrumb trail (spec §23): a fixed-size ring of the emulator's most recent
// significant activities — last SVC, last IPC service+command, last GPU
// method, last shader hash. Crash reports include it so a per-title failure
// pinpoints exactly where execution stopped without needing a live debugger.
//
// Lock-free-ish: each slot is a single writer (the thread executing the guest),
// readers tolerate torn reads (crash path only; best-effort by design).
// ---------------------------------------------------------------------------
struct Breadcrumb {
    enum class Kind : u8 { None = 0, Svc = 1, Ipc = 2, Gpu = 3, Shader = 4, Loader = 5 };

    Kind kind{Kind::None};
    u32 a{0};                 // kind-dependent: svc id / gpu method / etc.
    u32 b{0};                 // kind-dependent: ipc command id / etc.
    u64 pc{0};                // guest PC at the call
    char detail[40]{};        // service name / method name (truncated, NUL-ok)

    void Set(Kind k, u32 va, u32 vb, u64 pc_, std::string_view det) noexcept {
        kind = k; a = va; b = vb; pc = pc_;
        const size_t n = det.size() < sizeof(detail) - 1 ? det.size() : sizeof(detail) - 1;
        __builtin_memcpy(detail, det.data(), n);
        detail[n] = '\0';
    }
};

class BreadcrumbTrail {
public:
    static constexpr size_t kSlots = 32;

    static void Push(const Breadcrumb& bc) noexcept {
        const u32 idx = next_++ % kSlots;
        slots_[idx] = bc;
    }

    static void PushSvc(u32 svc_id, u64 pc) noexcept {
        Breadcrumb bc; bc.Set(Breadcrumb::Kind::Svc, svc_id, 0, pc, "svc");
        Push(bc);
    }
    static void PushIpc(std::string_view service, u32 cmd_id, u64 pc) noexcept {
        Breadcrumb bc; bc.Set(Breadcrumb::Kind::Ipc, cmd_id, 0, pc, service);
        Push(bc);
    }
    static void PushGpu(u32 method, u32 argument) noexcept {
        Breadcrumb bc; bc.Set(Breadcrumb::Kind::Gpu, method, argument, 0, "maxwell");
        Push(bc);
    }
    static void PushShader(u64 hash) noexcept {
        Breadcrumb bc; bc.Set(Breadcrumb::Kind::Shader,
                              static_cast<u32>(hash), static_cast<u32>(hash >> 32), 0, "shader");
        Push(bc);
    }

    /// Copy out the trail oldest→newest with valid entries only.
    static size_t Snapshot(Breadcrumb* out, size_t max) noexcept;

private:
    static inline std::array<Breadcrumb, kSlots> slots_{};
    static inline std::atomic<u32> next_{0};
};

} // namespace nemu::core::debug
