#pragma once

#include "core/types.hpp"
#include "core/cpu/cpu_state.hpp"
#include "core/memory/virtual_memory.hpp"
#include "code_cache.hpp"
#include "x64_emitter.hpp"
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
    CodeCache code_cache_;
    X64Emitter emitter_;
    std::unordered_map<vaddr_t, JitBlockFn> block_map_;
    JitStats stats_{};
};

} // namespace nemu::core::cpu::jit
