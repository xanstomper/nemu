# Nemu Engineering Roadmap & Gating Criteria

## Gating Philosophy

Nemu enforces strict quality gates. A milestone cannot be marked complete without passing its predefined automated test suite and independent architectural verification.

---

## Phase Breakdown & Gating

### Gate 0: Environment & Toolchain Baseline
* **Status:** **PASSED**
* **Criteria:**
  * Clean repository structure with subsystem isolation.
  * GCC 13.3.0 and MinGW-w64 toolchains verified.
  * CMake 3.28 and Ninja build configurations established.
  * Storage device inspection and non-destructive disk strategy confirmed.
  * Architectural documentation complete across all subsystems.

### Gate 1: CPU Architecture & Reference Interpreter
* **Objective:** Smallest real ARM64 execution core capable of deterministic instruction execution.
* **Criteria:**
  * Full 64-bit integer register file (`X0`..`X30`, `SP`, `PC`, `NZCV`).
  * Arithmetic/Logical instructions (`ADD`, `SUB`, `AND`, `ORR`, `EOR`, `LSL`, `LSR`, `ASR`, `ROR`).
  * Control flow (`B`, `B.cond`, `BL`, `BLR`, `RET`, `CBZ`, `CBNZ`, `TBZ`, `TBNZ`).
  * Memory load/store (`LDR`, `STR`, `LDP`, `STP`, signed/unsigned, byte/half/word/doubleword).
  * Automated instruction test harness verifying each opcode against golden outputs.

### Gate 2: Virtual Memory Subsystem
* **Objective:** Multi-level paging and guest memory protection.
* **Criteria:**
  * 48-bit guest virtual address space management.
  * Radix page table supporting 4 KiB and 64 KiB pages.
  * Memory protection bit enforcement (`PROT_READ`, `PROT_WRITE`, `PROT_EXEC`).
  * Out-of-bounds access trapping and atomic memory primitives (`LDXR`, `STXR`, `CAS`).

### Gate 3: Horizon OS Kernel Foundation (HLE)
* **Objective:** Minimum supervisor call (SVC) environment for single-process homebrew.
* **Criteria:**
  * Memory management SVCs (`svcSetHeapSize`, `svcAllocateMemory`, `svcMapMemory`).
  * Thread management SVCs (`svcCreateThread`, `svcStartThread`, `svcExitThread`, `svcSleepThread`).
  * Synchronization primitives (`svcWaitSynchronization`, `svcSignalEvent`, `svcClearEvent`).
  * Handle table management and thread-local storage (TLS).

### Gate 4: Dynamic Recompiler (JIT) Foundation
* **Objective:** High-performance ARM64 to x86-64 runtime code generation.
* **Criteria:**
  * Basic block identification, compilation, and code caching.
  * Dynarec memory allocation conforming to Xbox Dev Mode executable permissions.
  * Differential testing: 100% register state agreement between Interpreter and JIT across all unit tests.
  * Invalidation of code blocks upon self-modifying code or module reload.

### Gate 5: Direct3D 12 GPU Translation Engine
* **Objective:** Real graphics output via native Xbox D3D12 backend.
* **Criteria:**
  * Direct3D 12 device creation, command queue, and swapchain setup.
  * Maxwell 3D pushbuffer processing and subchannel state tracking.
  * Vertex buffer and index buffer upload and binding.
  * Basic shader translation from Maxwell NVN bytecode to HLSL/DXIL.
  * Presentation of rendered frame to screen with VSync pacing.

### Gate 6: Audio & Input Systems
* **Objective:** Low-latency audio playback and native Xbox controller handling.
* **Criteria:**
  * `audren:u` / `audout:u` buffer queue processing with 48kHz stereo mixing.
  * XAudio2 voice and stream output without underrun or audio stutter.
  * Switch HID shared memory updates driven by `Windows.Gaming.Input`.
  * Analog deadzone calibration and full button mapping.

### Gate 7: Homebrew Execution (First Real Target)
* **Objective:** Loading and running real, uncompressed Switch homebrew NRO binaries.
* **Criteria:**
  * VFS loading and relocation of `.text`, `.rodata`, `.data`, `.bss` sections.
  * Execution of homebrew entry point, basic memory allocations, and clean exit.
  * Display of interactive homebrew UI via D3D12.

### Gate 8: Xbox Series S/X Packaging & Deployment
* **Objective:** Validated installation and execution on real Xbox hardware in Developer Mode.
* **Criteria:**
  * Automated APPX/MSIX manifest generation with `runFullTrust` and `expandedResources`.
  * Successful packaging using clean dependency isolation.
  * Sideloading via Xbox Device Portal and successful boot to Nemu menu.

### Gate 9: Performance Profiling & Optimization
* **Objective:** Rock-solid frame pacing and minimal latency.
* **Criteria:**
  * Profiling hooks measuring CPU, JIT compilation, GPU submission, and frame times.
  * Frame-time variance < 2ms under steady-state rendering.
  * Fastmem acceleration enabled and validated on host and Xbox.

### Gate 10: Compatibility Matrix & Stability
* **Objective:** Transparent regression testing across diverse homebrew workloads.
* **Criteria:**
  * Automated execution of homebrew test suite.
  * Zero memory leaks over multi-hour runs.
  * Comprehensive crash reporting with guest PC, registers, and call stack.

### Gate 11: Release Candidate
* **Objective:** Production-grade release for the Xbox Developer Mode community.
* **Criteria:**
  * Independent architectural and QA sign-off by AGY.
  * Full documentation, user guide, and clean build instructions.
