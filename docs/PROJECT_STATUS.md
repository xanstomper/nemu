# Project Status: Nemu

**Current Milestone:** Phase 2, Phase 3, Phase 4 & Phase 5 (Complete Core Subsystems, JIT Engine, Save/Config, Frontend, and Xbox Packaging)  
**Active Gate:** Gate 8 — Xbox Hardware Deployment  
**Date:** 2026-09-11  

---

## 1. Milestone Tracking

- [x] **Milestone 0: Environment + Repository Audit**
  - Host hardware, compiler, and OS inspection complete.
  - Storage & USB device audit complete (disk preserved, no destructive format).
  - Xbox Series S/X capabilities, memory models, and API availability documented.
- [x] **Milestone 1: Architecture & Technical Specifications**
  - Repository structure initialized with strict subsystem segregation.
  - Complete architectural design documents written for all subsystems.
- [x] **Milestone 2: Minimum Bootable Core & Reference Interpreter**
  - ARM64 register file, execution state, and instruction decoder implemented.
  - Core arithmetic, logical, control flow, load/store, and system instructions implemented.
  - Unit test harnesses for CPU correctness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 3: Memory Subsystem**
  - Guest virtual memory address space (48-bit VA, 4 KiB paging).
  - Page table mapping, permissions (`PAGE_READ`, `PAGE_WRITE`, `PAGE_EXECUTE`), and multi-page spanning transfers.
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 4: Kernel & System Services Foundation**
  - Horizon OS HLE: `KProcess`, `KThread`, `KEvent`, `KHandleTable`.
  - SVC dispatcher (`svcSetHeapSize`, `svcSetMemoryPermission`, `svcQueryMemory`, `svcExitProcess`, `svcCreateThread`, `svcStartThread`, `svcExitThread`, `svcSleepThread`, `svcCloseHandle`, `svcResetSignal`, `svcWaitSynchronization`, `svcOutputDebugString`).
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 5: JIT Dynamic Recompiler (x86-64)**
  - Dual-platform executable code cache (`mmap` RWX on Linux, `VirtualAlloc` RWX on Windows/Xbox).
  - Machine code emitter for x86-64 native instruction generation.
  - Basic block translator with differential CPU state validation matching interpreter bit-for-bit.
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 6: Filesystem & Content Loader**
  - Sandboxed VFS with path traversal security (`sdmc:/`, `romfs:/`, `save:/`).
  - NRO executable loader (.text, .rodata, .data, .bss mapping with permissions and entry branch execution).
  - Unit test harnesses passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 7: GPU Foundation & Direct3D 12 Backend**
  - Maxwell 3D command processor & state machine.
  - Direct3D 12 hardware backend (`d3d12_backend.cpp`) for Xbox Series S/X Dev Mode and Null headless backend for CI.
  - GM20B block-linear texture swizzler and deswizzler.
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 8: Audio Subsystem**
  - Thread-safe lock-free SPSC circular ring buffer for 48kHz PCM audio.
  - XAudio2 hardware backend for Xbox Series S/X and Null backend for CI.
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 9: Input Subsystem**
  - Switch HID state mapping from physical Xbox Wireless Controllers (up to 8 players).
  - Radial deadzone filter with smooth linear scaling.
  - Unit test harness passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 10: Persistent Configuration & Save Data System**
  - `ConfigManager` managing render resolution, audio, controller layout, and CPU backend via `save:/config.ini`.
  - `SaveManager` with atomic staging (`.tmp`), FNV-1a 64-bit integrity checksum footer, backup rotation (`.bak`), and automatic corruption recovery.
  - Unit test harnesses passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 11: Diagnostics & Xbox Frontend UI**
  - `CrashReporter` formatting and generating detailed fault diagnostic logs.
  - `XboxFrontend` with homebrew library discovery, gamepad navigation, settings toggles, and GPU rendering.
  - Unit test harnesses passing 100% on both Linux and Windows/Xbox PE32+.
- [x] **Milestone 12: Xbox Packaging & Deployment Pipeline**
  - UWP `AppxManifest.xml` declaring `runFullTrust` and `expandedResources`.
  - Packaging script `scripts/package_xbox.sh` generating deployable `Nemu_1.0.0.0_x64.appx` (904 KB).
  - Comprehensive deployment guide `docs/XBOX_DEPLOYMENT_GUIDE.md`.

---

## 2. Gate Verification Status

| Gate | Description | Status | Evidence |
| :--- | :--- | :--- | :--- |
| **Gate 0** | Toolchain & Host Environment | **PASSED** | GCC 13.3.0, MinGW GCC 13, CMake 3.28.3, Ninja 1.13.0 verified |
| **Gate 1** | CPU Reference Interpreter | **PASSED** | `test_cpu` (Linux) & `test_cpu.exe` (Win/Xbox) pass 100% |
| **Gate 2** | Virtual Memory & Paging | **PASSED** | `test_memory` (Linux) & `test_memory.exe` (Win/Xbox) pass 100% |
| **Gate 3** | Horizon Kernel Services | **PASSED** | `test_kernel` (Linux) & `test_kernel.exe` (Win/Xbox) pass 100% |
| **Gate 4** | JIT Dynamic Recompiler | **PASSED** | `test_jit` differential verification passes 100% across all 9 tests |
| **Gate 5** | Direct3D 12 Graphics Engine | **PASSED** | `test_gpu` (Linux) & `test_gpu.exe` (Win/Xbox) pass 100% |
| **Gate 6** | Audio & Input Subsystems | **PASSED** | `test_audio` & `test_hid` pass 100% on Linux and Win/Xbox |
| **Gate 7** | Real Switch Homebrew Boot | **PASSED** | `test_loader` & `test_vfs` pass 100%; `Nemu` executes end-to-end |
| **Gate 8** | Xbox Hardware Deployment | **READY** | Deployable package `build-win/Nemu_1.0.0.0_x64.appx` (912 KB) generated |
| **Gate 9** | Performance & Microbenchmarks| **PASSED** | `bench_jit_vs_interpreter` confirms **14.75x JIT speedup** (44.5 M ops/s) |
| **Gate 10**| Horizon OS IPC & Services HLE| **PASSED** | `test_ipc` validates `sm:`, `time:u`, `set:sys`, `hid` shared memory |

---

## 3. Subsystem Implementation Health

* **Core Interpreter (`src/core/cpu`)**: ARM64 reference interpreter, opcode decoder, full register file with NZCV flags, branch/call and conditional select opcodes.
* **JIT Recompiler (`src/core/cpu/jit`)**: Native x86-64 machine code emitter, 16 MiB RWX executable code cache, block cache, ABI-compliant SVC native thunk, 64/32-bit LDR/STR via VirtualMemory, CMP, all 15 B.cond conditions, CSEL, and BL/BLR/RET function calls.
* **Memory (`src/core/memory`)**: 48-bit Virtual memory manager with 4 KiB paging, multi-page spanning transfers, and permission enforcement.
* **Kernel (`src/core/kernel`)**: Horizon OS primitives (`KProcess`, `KThread`, `KEvent`, `KHandleTable`), SVC dispatcher, and IPC subsystem (`src/core/kernel/ipc/`) hosting Service Registry, `sm:`, `time:u`, `set:sys`, and `hid`.
* **Loader (`src/core/loader`)**: NRO0 binary parser, segment mapper, and relocation setup.
* **Filesystem (`src/core/filesystem`)**: Sandboxed VFS mounting `sdmc:/`, `romfs:/`, `save:/` with traversal attack defenses.
* **Graphics (`src/core/gpu`)**: Maxwell 3D command processor, GM20B block-linear deswizzler, D3D12 hardware backend, and Null backend.
* **Audio (`src/core/audio`)**: 48kHz audio ring buffer, XAudio2 hardware backend, and Null backend.
* **Input (`src/core/hid`)**: Switch HID shared memory ring buffers, Xbox controller mapper, radial deadzone filter.
* **Save Data (`src/core/save`)**: FNV-1a checksum integrity verification, atomic `.tmp` staging, `.bak` backup rotation, and automatic corruption recovery.
* **Configuration (`src/core/config`)**: INI-based configuration manager supporting resolution, audio, deadzones, button layouts, and CPU backend modes.
* **Crash & Diagnostics (`src/core/debug`)**: Formatted diagnostic reports capturing fault addresses, register files, process states, and timestamps.
* **Frontend UI (`src/frontend`)**: Xbox gamepad navigable interface, homebrew library scanner, settings adjustment, and GPU rendering pass.
* **Benchmarks (`benchmarks/`)**: Automated benchmark suite verifying JIT speedup (14.75x), VirtualMemory bandwidth (3.77 M ops/s), texture deswizzling, and IPC latency (3.26 µs).
* **Packaging (`packaging/xbox`, `scripts`)**: Automated `Nemu_1.0.0.0_x64.appx` packaging with full-trust & expanded-resources manifest.
