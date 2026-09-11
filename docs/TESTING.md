# Nemu Testing & Verification Strategy

## 1. Quality Standards

To prevent regressions and verify emulation accuracy, Nemu adheres to a strict multi-tier verification hierarchy:

```
┌─────────────────────────────────────────────────────────────┐
│                 End-to-End Homebrew Tests                   │
│           (Boots real NRO, renders frames, exits)           │
├─────────────────────────────────────────────────────────────┤
│                 Differential Verification                   │
│       (Compares Interpreter vs. JIT Register States)        │
├─────────────────────────────────────────────────────────────┤
│                    Integration Tests                        │
│         (CPU + Memory MMU, Kernel SVCs + Threads)           │
├─────────────────────────────────────────────────────────────┤
│                       Unit Tests                            │
│    (ARM64 Instruction Opcodes, VFS Parsing, Audio Ring)     │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Unit Testing Suite

Located under `tests/unit/`:

* **CPU Opcodes (`tests/unit/cpu/`):**
  * Immediate arithmetic (`ADD`, `SUB`, `CMP`, `CMN`).
  * Logical operations (`AND`, `ORR`, `EOR`, `TST`, `BIC`).
  * Shift and rotate (`LSL`, `LSR`, `ASR`, `ROR`).
  * Bitfield manipulation (`UBFM`, `SBFM`, `EXTR`).
  * Conditional branching (`B.EQ`, `B.NE`, `B.CS`, `B.CC`, `B.MI`, `B.PL`, `B.VS`, `B.VC`, `B.HI`, `B.LS`, `B.GE`, `B.LT`, `B.GT`, `B.LE`).
  * Load and store (`LDR`, `STR`, `LDP`, `STP`, unscaled, post-indexed, pre-indexed).
  * Flag verification (exhaustive checking of `N`, `Z`, `C`, `V` bits).
* **Virtual Memory (`tests/unit/memory/`):**
  * Page table allocation and mapping.
  * Permission enforcement (`PROT_NONE`, `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`).
  * Boundary conditions, alignment violations, and multi-page spanning reads/writes.
* **Kernel & Handles (`tests/unit/kernel/`):**
  * Handle table insertion, lookup, retention, and closure.
  * Thread state machine transitions.
  * Synchronization object signaling and timeouts.

---

## 3. Differential Testing (Interpreter vs. JIT)

Differential testing ensures that the Dynarec JIT produces results 100% equivalent to the reference interpreter:
1. Initialize test `CpuState` with pseudorandom seed.
2. Load random valid ARM64 instruction sequences into guest memory.
3. Duplicate state into `interp_state` and `jit_state`.
4. Execute via Reference Interpreter; record final registers and `NZCV`.
5. Execute via Dynarec JIT; record final registers and `NZCV`.
6. Assert `memcmp(&interp_state, &jit_state, sizeof(CpuState)) == 0`.
7. On discrepancy, log exact disassembly, input registers, and divergent outputs.

---

## 4. End-to-End Homebrew Verification

Located under `tests/e2e/`:
* Loads uncompressed Nintendo Switch homebrew binaries (e.g. `hello_world.nro`, `libnx_test.nro`).
* Verifies:
  1. Section headers (`.text`, `.rodata`, `.data`, `.bss`) relocate accurately.
  2. Main thread initializes TLS and enters entry point.
  3. `svcSetHeapSize` allocates guest dynamic heap.
  4. Framebuffer outputs expected test colors/patterns via D3D12.
  5. Application cleanly calls `svcExitProcess` returning code 0.
