#pragma once

#include "core/types.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/memory/virtual_memory.hpp"
#include "code_cache.hpp"
#include "x64_emitter.hpp"
#include <functional>
#include <unordered_map>
#include <memory>

namespace nemu::core::cpu::jit {

using JitBlockFn = void (*)(CpuState* state);

struct JitStats {
    u64 blocks_compiled{0};
    u64 blocks_executed{0};
    u64 instructions_recompiled{0};
};

class JitCompiler {
public:
    explicit JitCompiler(size_t cache_size = CodeCache::DEFAULT_CACHE_SIZE);
    ~JitCompiler() = default;

    JitCompiler(const JitCompiler&) = delete;
    JitCompiler& operator=(const JitCompiler&) = delete;

    using SvcHandler = std::function<void(CpuState&, u32)>;

    /// Register the Horizon SVC handler invoked when a compiled block executes
    /// an SVC instruction. The handler address is captured at block compile
    /// time; call SetSvcHandler() before compiling any blocks that contain SVCs.
    /// Shared across JIT instances (a single emulator owns the guest SVC path).
    static void SetSvcHandler(SvcHandler handler) { svc_handler_ = std::move(handler); }
    [[nodiscard]] static bool HasSvcHandler() noexcept { return static_cast<bool>(svc_handler_); }

    /// Compile a basic block starting at guest_pc if not already cached
    JitBlockFn CompileBlock(vaddr_t guest_pc, memory::VirtualMemory& memory);

    /// Execute a compiled basic block on guest state
    bool Execute(CpuState& state, memory::VirtualMemory& memory);

    /// Invalidate compiled block at specific PC or entire cache
    void InvalidateBlock(vaddr_t guest_pc);
    void Clear();

    [[nodiscard]] const JitStats& GetStats() const noexcept { return stats_; }
    [[nodiscard]] size_t GetCachedBlockCount() const noexcept { return block_map_.size(); }

private:
    /// Native thunk called from JIT-generated code on an SVC. Returns to the
    /// generated block after the handler completes, preserving callee-saved
    /// registers. `state` points at the guest CpuState (in R15 across the call).
    static void InvokeSvcHandler(CpuState* state, u32 svc_id);

    // --- Instruction emission helpers (emit against emitter_, R15 = CpuState*) ---

    /// Guest CpuState slot for integer register Xn (0..30).
    static s32 XSlot(u8 reg) noexcept { return static_cast<s32>(reg) * 8; }
    /// SP-context slot: Xn -> x[n], 31 -> sp.
    s32 RegOrSpSlot(u8 reg) const noexcept;
    /// Emit SetX(rd, val); a write to XZR (31) is discarded.
    void EmitSetX(u8 rd, X64Reg val);
    /// Emit SetW(rd, val); val must already be zero-extended to 64 bits.
    void EmitSetW(u8 rd, X64Reg val);
    /// Store N/Z/C/V into CpuState.pstate using the x86 flags produced by the
    /// preceding integer subtraction (ARM C = !CF, V = OF, N = SF, Z = ZF).
    void EmitSetNZCVFromSub();
    /// Apply the guest register-shift encoded in (shift_type, shift_amount) to
    /// the 64-bit value currently in RDX, matching the interpreter's ApplyShift.
    void EmitShiftRegToRdx(bool is_64bit, u8 shift_type, u8 shift_amount);
    /// Emit code that leaves ZF == 1 iff the ARM condition holds, using
    /// `cmp al, 1` as the final flag-setting step (AL = 0/1 boolean).
    void EmitConditionToZF(Condition cond);
    /// Emit the VirtualMemory-backed load/store for an unsigned-offset access.
    void EmitMemAccess(bool is_load, bool is_64bit, u8 rn, u64 offset, u8 rt);
    /// Emit a uniform 6-argument call to the JitSlowOp dispatcher thunk,
    /// following the correct ABI stack/shadow/alignment rules for the target.
    void EmitSlowCall(u64 op, u64 rd, u64 rn, u64 rm, u64 rs, u64 rt2,
                      u64 vec_size, u64 vec_index, u64 is64, u64 is_dbl,
                      u64 cond, u64 is_load, u64 imm, u64 fp_imm_bits);

    /// Captured guest VirtualMemory address, patched into generated code at
    /// block compile time. The block cache is only valid while the same
    /// VirtualMemory object backs execution.
    u64 mem_addr_{0};

    CodeCache code_cache_;
    X64Emitter emitter_;
    std::unordered_map<vaddr_t, JitBlockFn> block_map_;
    JitStats stats_{};
    static SvcHandler svc_handler_;
};

} // namespace nemu::core::cpu::jit
