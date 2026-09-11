#include "jit_compiler.hpp"
#include "core/cpu/decoder.hpp"
#include "platform/logger.hpp"
#include <cstring>

namespace nemu::core::cpu::jit {

namespace {
    constexpr s32 OFFSET_PC = 256;
    constexpr size_t MAX_BLOCK_INSTRUCTIONS = 32;
}

JitCompiler::JitCompiler(size_t cache_size)
    : code_cache_(cache_size) {
}

JitBlockFn JitCompiler::CompileBlock(vaddr_t guest_pc, memory::VirtualMemory& memory) {
    auto it = block_map_.find(guest_pc);
    if (it != block_map_.end()) {
        return it->second;
    }

    emitter_.Clear();

    // 1. Prologue: save callee-saved registers
    emitter_.Push(X64Reg::RBX);
    emitter_.Push(X64Reg::RBP);
    emitter_.Push(X64Reg::R12);
    emitter_.Push(X64Reg::R13);
    emitter_.Push(X64Reg::R14);
    emitter_.Push(X64Reg::R15);

    // Load CpuState* into R15
#ifdef _WIN32
    // Windows x64 ABI: 1st argument in RCX
    emitter_.MovR64R64(X64Reg::R15, X64Reg::RCX);
#else
    // System V x64 ABI: 1st argument in RDI
    emitter_.MovR64R64(X64Reg::R15, X64Reg::RDI);
#endif

    // 2. Decode & Translate guest instructions
    vaddr_t curr_pc = guest_pc;
    bool block_ended = false;
    size_t insn_count = 0;

    while (!block_ended && insn_count < MAX_BLOCK_INSTRUCTIONS) {
        if (!memory.IsValidAddress(curr_pc, 4)) {
            break;
        }

        const u32 raw_insn = memory.Read32(curr_pc);
        const DecodedInstruction inst = Decoder::Decode(raw_insn);
        insn_count++;

        switch (inst.opcode) {
            case Opcode::MOVZ: {
                const u64 val = inst.imm << inst.shift_amount;
                if (inst.rd != 31) {
                    emitter_.MovR64Imm(X64Reg::RAX, val);
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ADD_imm: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.AddR64Imm32(X64Reg::RAX, static_cast<s32>(inst.imm));
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ADD_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.AddR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::SUB_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.SubR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::AND_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.AndR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::ORR_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.OrR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::EOR_reg: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovR64Mem(X64Reg::RDX, X64Reg::R15, static_cast<s32>(inst.rm * 8));
                emitter_.XorR64R64(X64Reg::RAX, X64Reg::RDX);
                if (inst.rd != 31) {
                    emitter_.MovMemR64(X64Reg::R15, static_cast<s32>(inst.rd * 8), X64Reg::RAX);
                }
                curr_pc += 4;
                break;
            }

            case Opcode::NOP: {
                curr_pc += 4;
                break;
            }

            case Opcode::B: {
                const vaddr_t target = curr_pc + inst.imm;
                emitter_.MovR64Imm(X64Reg::RAX, target);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::RET: {
                emitter_.MovR64Mem(X64Reg::RAX, X64Reg::R15, static_cast<s32>(inst.rn * 8));
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            case Opcode::SVC: {
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
            }

            default:
                // Unsupported opcode terminates block
                emitter_.MovR64Imm(X64Reg::RAX, curr_pc);
                emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
                block_ended = true;
                break;
        }
    }

    if (!block_ended) {
        emitter_.MovR64Imm(X64Reg::RAX, curr_pc);
        emitter_.MovMemR64(X64Reg::R15, OFFSET_PC, X64Reg::RAX);
    }

    // 3. Epilogue: restore callee-saved registers and return
    emitter_.Pop(X64Reg::R15);
    emitter_.Pop(X64Reg::R14);
    emitter_.Pop(X64Reg::R13);
    emitter_.Pop(X64Reg::R12);
    emitter_.Pop(X64Reg::RBP);
    emitter_.Pop(X64Reg::RBX);
    emitter_.Ret();

    // 4. Allocate into code cache
    u8* exec_ptr = code_cache_.Allocate(emitter_.GetSize());
    if (!exec_ptr) {
        NEMU_LOG_ERROR("JIT", "Failed to allocate code cache buffer for block 0x{:016X}", guest_pc);
        return nullptr;
    }

    std::memcpy(exec_ptr, emitter_.GetCode().data(), emitter_.GetSize());
    code_cache_.Flush(exec_ptr, emitter_.GetSize());

    auto fn = reinterpret_cast<JitBlockFn>(exec_ptr);
    block_map_[guest_pc] = fn;
    stats_.blocks_compiled++;
    stats_.instructions_recompiled += insn_count;

    NEMU_LOG_DEBUG("JIT", "Compiled basic block at 0x{:016X} ({} insns, {} bytes x86-64)",
        guest_pc, insn_count, emitter_.GetSize());

    return fn;
}

bool JitCompiler::Execute(CpuState& state, memory::VirtualMemory& memory) {
    JitBlockFn fn = CompileBlock(state.pc, memory);
    if (!fn) return false;

    fn(&state);
    stats_.blocks_executed++;
    return true;
}

void JitCompiler::InvalidateBlock(vaddr_t guest_pc) {
    block_map_.erase(guest_pc);
}

void JitCompiler::Clear() {
    block_map_.clear();
    code_cache_.Reset();
}

} // namespace nemu::core::cpu::jit
