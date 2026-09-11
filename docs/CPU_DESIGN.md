# Nemu CPU Subsystem Architecture & Design

## 1. Specification & Scope

The CPU subsystem models the 64-bit ARMv8.0-A architecture (AArch64 execution state) as found in the Nintendo Switch's custom NVIDIA Tegra X1 SoC (4x Cortex-A57 cores).

Key Invariants:
* Pure 64-bit AArch64 execution (AArch32 is not used for retail Switch titles/homebrew).
* Little-endian data and code memory ordering.
* Hardware registers and state are strictly encapsulated in `nemu::core::cpu::CpuState`.

---

## 2. Register State Architecture

```cpp
struct alignas(16) CpuState {
    // 31 General Purpose Registers (X0 - X30)
    // Register 31 is mapped to Zero Register (XZR) or Stack Pointer (SP) depending on instruction context
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;

    // Processor State Flags (NZCV)
    struct {
        uint8_t n : 1; // Negative
        uint8_t z : 1; // Zero
        uint8_t c : 1; // Carry
        uint8_t v : 1; // Overflow
    } pstate;

    // 32 128-bit SIMD & Floating Point Registers (V0 - V31)
    alignas(16) uint128_t v[32];

    // Floating Point Control and Status Registers
    uint32_t fpcr;
    uint32_t fpsr;

    // System Registers
    uint64_t tpidrro_el0; // Read-only thread ID (used by Switch TLS)
    uint64_t tpidr_el0;   // Read-write thread ID
    uint64_t cntfrq_el0;  // System counter frequency (19.2 MHz on Switch)
};
```

---

## 3. Instruction Decoding Hierarchy

ARM64 instructions are fixed 32-bit words, partitioned by bits `[28:25]`:

| Bits [28:25] | Major Instruction Group | Class Handler |
| :--- | :--- | :--- |
| `0000` | Reserved / Unallocated | `DecodeReserved` |
| `0001` | Unallocated | `DecodeReserved` |
| `0010` | SVE / Advanced SIMD | `DecodeSimd` |
| `0011` | Unallocated | `DecodeReserved` |
| `100x` | Data Processing — Immediate | `DecodeDataProcImm` |
| `101x` | Branches, Exception Generation, System | `DecodeBranchSystem` |
| `x1x0` | Loads and Stores | `DecodeLoadStore` |
| `x101` | Data Processing — Register | `DecodeDataProcReg` |
| `x111` | Data Processing — Scalar Floating-Point & Advanced SIMD | `DecodeDataProcFp` |

---

## 4. Reference Interpreter Design

The reference interpreter serves as the golden model for emulation correctness.

### Deterministic Execution Loop
1. Fetch 32-bit instruction word: `uint32_t raw_inst = memory.Read32(cpu.pc);`
2. Advance PC: `cpu.pc += 4;`
3. Identify opcode via bitmask matching.
4. Execute semantic handler updating register state and condition flags.
5. Check for interrupts / thread reschedule requests.

### Arithmetic & Flag Calculation
Flags `N`, `Z`, `C`, `V` are computed precisely according to ARM ARM DDI 0487:
* **N (Negative):** Most significant bit (`result >> 63` or `result >> 31`).
* **Z (Zero):** `result == 0`.
* **C (Carry):** For addition: `result < op1`. For subtraction: `op1 >= op2` (borrow inverted).
* **V (Overflow):** Signed integer overflow detected when signs of inputs agree but differ from the result sign.

---

## 5. Verification & Testing Strategy

* **Unit Tests:** Direct test cases executing opcodes and checking `CpuState` against precomputed expected values.
* **Differential Testing:** Test vectors run identically through the Interpreter and JIT, comparing post-instruction registers, SP, and NZCV flags.
