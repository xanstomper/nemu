# Nemu JIT Dynamic Recompiler Architecture

## 1. Overview

Nemu employs a tiered execution architecture to reconcile deterministic correctness with high-performance emulation on the AMD Zen 2 CPU of Xbox Series S/X.

* **Tier 0:** Reference Interpreter (Pure C++20, gold standard for correctness).
* **Tier 1:** Baseline JIT (Fast basic-block compiler targeting host x86-64).
* **Tier 2:** Block Linking & Fastmem Acceleration (Optimized register pinning and direct branch chaining).

---

## 2. Recompilation Pipeline

```
ARM64 Guest Code (32-bit instructions)
                 │
                 ▼
         Block Disassembler
   (Scans instructions until terminating branch/ret)
                 │
                 ▼
     Intermediate Representation (IR)
  (SSA-form micro-ops: Load, Store, Add, Sub, Branch)
                 │
                 ▼
     Host Register Allocator
 (Pins frequent ARM64 registers to x86-64 GPRs)
                 │
                 ▼
       x86-64 Code Generator
  (Emits native x86-64 instructions into Code Cache)
                 │
                 ▼
         Code Cache & Hash Map
 (VA -> Executable Host Pointer Lookup)
```

---

## 3. Host Register Allocation Strategy (x86-64 Target)

The AMD Zen 2 host CPU provides 16 64-bit general-purpose registers (`RAX`, `RCX`, `RDX`, `RBX`, `RSP`, `RBP`, `RSI`, `RDI`, `R8`..`R15`).

Nemu maps these registers according to a strict calling convention:

| x86-64 Register | Role in Nemu JIT |
| :--- | :--- |
| `R15` | **CpuState Base Pointer** (Points to active `CpuState` struct) |
| `R14` | **Fastmem Base Pointer** (Base of guest virtual memory address space) |
| `R13` | **Guest Program Counter (PC)** |
| `R12` | **Guest Stack Pointer (SP)** |
| `R11` | Scratch register / temporary |
| `R10` | Scratch register / temporary |
| `RAX`, `RCX`, `RDX` | Scratch registers & host function call parameters |
| `RBX`, `RSI`, `RDI`, `R8`, `R9` | Dynamically assigned to active guest ARM64 registers (`X0`..`X30`) |

---

## 4. Executable Code Cache & Xbox Dev Mode Compliance

1. **Memory Allocation:**
   * Allocates code cache chunks using `VirtualAlloc(nullptr, CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)`.
2. **Translation & Emission:**
   * Emits machine code into the writable buffer.
3. **Execution Transition:**
   * Uses `VirtualProtect(buffer, CHUNK_SIZE, PAGE_EXECUTE_READ, &old_protect)` to transition pages to executable.
   * Invokes `FlushInstructionCache(GetCurrentProcess(), buffer, CHUNK_SIZE)` to ensure host CPU L1i / L2 instruction caches are synchronized.
4. **Block Lookup Table:**
   * A direct-mapped hash table indexed by `GuestPC >> 2` stores the host code pointer. If cache miss occurs, execution falls back to the dynamic recompiler or interpreter.

---

## 5. Self-Modifying Code & Invalidation

When guest software writes to an address containing executable code (e.g. dynamic linking, JIT execution inside the guest, or overlay loading):
* The virtual memory page containing the code is marked write-protected (`PAGE_READONLY`).
* When the guest writes to that page, a fault occurs.
* Nemu intercepts the fault, invalidates all compiled basic blocks originating within that page, unprotects the page, and resumes execution.
